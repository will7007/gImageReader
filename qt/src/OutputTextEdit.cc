/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * OutputTextEdit.cc
 * Copyright (C) 2013-2020 Sandro Mani <manisandro@gmail.com>
 *
 * gImageReader is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gImageReader is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "OutputTextEdit.hh"

#include <QApplication>
#include <QEventLoop>
#include <QPainter>
#include <QSyntaxHighlighter>
#include <qregularexpression.h>

class OutputTextEdit::WhitespaceHighlighter : public QSyntaxHighlighter {
public:
	WhitespaceHighlighter(QTextDocument* document)
		: QSyntaxHighlighter(document) {}
private:
	void highlightBlock(const QString& text) {
		QTextCharFormat fmt;
		fmt.setForeground(Qt::gray);

		QRegExp expression("\\s");
		int index = text.indexOf(expression);
		while (index >= 0) {
			int length = expression.matchedLength();
			setFormat(index, length, fmt);
			index = text.indexOf(expression, index + length);
		}
	}
};


OutputTextEdit::OutputTextEdit(QWidget* parent)
	: QPlainTextEdit(parent) {
	m_wsHighlighter = new WhitespaceHighlighter(document());

	m_regionCursor = textCursor();
	m_regionCursor.movePosition(QTextCursor::Start);
    m_regionCursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);  // selects the region in blue/like shift+arrows
	m_entireRegion = true;

	connect(this, &OutputTextEdit::cursorPositionChanged, this, &OutputTextEdit::saveRegionBounds);
	connect(this, &OutputTextEdit::selectionChanged, this, &OutputTextEdit::saveRegionBounds);

	// Force inactive selection to have same color as active selection
	QColor highlightColor = palette().color(QPalette::Highlight);
	QColor highlightedTextColor = palette().color(QPalette::HighlightedText);
	auto colorToString = [&](const QColor & color) {
		return QString("rgb(%1, %2, %3)").arg(color.red()).arg(color.green()).arg(color.blue());
	};
	setStyleSheet(QString("QPlainTextEdit { selection-background-color: %1; selection-color: %2; }")
	              .arg(colorToString(highlightColor), colorToString(highlightedTextColor)));
}

OutputTextEdit::~OutputTextEdit() {
	delete m_wsHighlighter;
}

QTextCursor OutputTextEdit::regionBounds() const {
	QTextCursor c = m_regionCursor;
	if(c.anchor() > c.position()) {
		int pos = c.anchor();
		int anchor = c.position();
		c.setPosition(anchor);
		c.setPosition(pos, QTextCursor::KeepAnchor);
	}
	return c;
}


bool OutputTextEdit::findReplace(bool backwards, bool replace, bool matchCase, const QString& searchstr, const QString& replacestr) {
    clearFocus();
    bool regexSearch = true;
    QRegularExpression regex;
    if(regexSearch) {
        regex = QRegularExpression(searchstr);
    }

    if(searchstr.isEmpty()) {
        return false;
    }

    QTextDocument::FindFlags flags;
    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    if(backwards) {
        flags |= QTextDocument::FindBackward;
    }
    if(matchCase) {
        flags |= QTextDocument::FindCaseSensitively;
        cs = Qt::CaseSensitive;
    }
    QTextCursor regionCursor = regionBounds();
    QTextCursor selCursor = textCursor();

    bool isCurrentSelectionAMatch;
    if(regexSearch) {
        isCurrentSelectionAMatch = regex.match(selCursor.selectedText(), flags).hasMatch();
    } else {
        isCurrentSelectionAMatch = selCursor.selectedText().compare(searchstr, cs) == 0;
    }

    if(isCurrentSelectionAMatch) {
        if(replace) {
            bool atDocumentStart = selCursor.anchor() == 0 || selCursor.position() == 0;
            selCursor.insertText(replacestr);

            if(atDocumentStart) {
                // workaround to prevent all following other text from getting highlighted blue and causing a crash if the QDocument is tabbed to
                // (only happens when inserting text at document start)
                selCursor.document()->setPlainText(selCursor.document()->toRawText());
                regionCursor = regionBounds();
                selCursor.setPosition(regionCursor.anchor() + replacestr.length());
            }

            selCursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor, replacestr.length());
            setTextCursor(selCursor);
            ensureCursorVisible();
            return true;
        }

        if(backwards) {
            selCursor.setPosition(std::min(selCursor.position(), selCursor.anchor()));
        } else {
            selCursor.setPosition(std::max(selCursor.position(), selCursor.anchor()));
        }
    }

    QTextCursor findcursor;
    if(regexSearch) {
        findcursor = document()->find(regex, selCursor, flags);
    } else {
        findcursor = document()->find(searchstr, selCursor, flags);
    }

    if(regexSearch && findcursor.position() == findcursor.anchor()) {  // if we matched nothing (.* or similar regex used)
        if(backwards) {
            if(findcursor.atBlockEnd() && selCursor.anchor() == (findcursor.positionInBlock() + 1)) {
                // need to see if the selCursor anchor == block start because that would mean we shrunk the right/end to nothing while the left/start stayed in place
                // selCursor.anchor().atBlockStart() doesn't exist but we know findcursor was atBlockEnd, i.e. the next block going backwards
                // so previousBlock().getBlockEnd()+1 == start of the next block
                // If we have no found selection (0 chars), we are in regex mode, but we did move (backwards)
                // then most likely we were looking for .* or similar and now we are stuck forever
                selCursor.setPosition(std::max(selCursor.position() - 1, regionCursor.anchor()));
                findcursor = document()->find(regex, selCursor, flags);
            }
        }
        else {
            // don't just constrain skipping to going over the end of the line: we can have an infinite fwd match
            // any time we don't move the cursor (either matching anything 0 times with .* or fwd lookahead causing us not to move)
            // If we have no found selection (0 chars), we did not move from the end of the block/line,
            // and we are in regex mode, then most likely we were looking for .* or similar and now we are stuck forever
            int newPosition = selCursor.position() + 1;  // try to skip over whatever was giving us trouble by going right of the position, going back to the beginning if we need to
            if(newPosition > regionCursor.position()) {  // >= would fail to match a final line with nothing on it (\n)
                selCursor.setPosition(regionCursor.anchor());
            }
            else {
                selCursor.setPosition(newPosition);
            }

            findcursor = document()->find(regex, selCursor, flags);
        }
    }

    if(findcursor.isNull() || !(findcursor.anchor() >= regionCursor.anchor() && findcursor.position() <= regionCursor.position())) {
        // check if we couldn't find anything because we need to loop around the document
        if(backwards) {
            selCursor.setPosition(regionCursor.position());  // we hit the beginning of the region, so go to the end of the entire text region
        } else {
            selCursor.setPosition(regionCursor.anchor());
        }
        if(regexSearch) {
            findcursor = document()->find(regex, selCursor, flags);
        } else {
            findcursor = document()->find(searchstr, selCursor, flags);
        }
        if(findcursor.isNull() || !(findcursor.anchor() >= regionCursor.anchor() && findcursor.position() <= regionCursor.position())) {
            return false;
        }
    }
    setTextCursor(findcursor);
    ensureCursorVisible();
    return true;
}

bool OutputTextEdit::replaceAll(const QString& searchstr, const QString& replacestr, bool matchCase) {
	QTextCursor cursor =  regionBounds();
	int end = cursor.position();
	QString cursel = cursor.selectedText();
	if(cursor.anchor() == cursor.position() ||
	        cursel == searchstr ||
	        cursel == replacestr) {
		cursor.movePosition(QTextCursor::Start);
		QTextCursor tmp(cursor);
		tmp.movePosition(QTextCursor::End);
		end = tmp.position();
	} else {
		cursor.setPosition(std::min(cursor.anchor(), cursor.position()));
	}
	QTextDocument::FindFlags flags;
	if(matchCase) {
		flags = QTextDocument::FindCaseSensitively;
	}
	int diff = replacestr.length() - searchstr.length();
	int count = 0;
	while(true) {
		cursor = document()->find(searchstr, cursor, flags);
		if(cursor.isNull() || cursor.position() > end) {
			break;
		}
		cursor.insertText(replacestr);
		end += diff;
		++count;
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	}
	if(count == 0) {
		return false;
	}
	return true;
}

void OutputTextEdit::setDrawWhitespace(bool drawWhitespace) {
	m_drawWhitespace = drawWhitespace;
	QTextOption textOption = document()->defaultTextOption();
	if(drawWhitespace) {
		textOption.setFlags(QTextOption::ShowTabsAndSpaces | QTextOption::AddSpaceForLineAndParagraphSeparators);
	} else {
		textOption.setFlags(QTextOption::Flags());
	}
	document()->setDefaultTextOption(textOption);
}

void OutputTextEdit::paintEvent(QPaintEvent* e) {
	QPointF offset = contentOffset();

	if(!m_entireRegion) {
		QPainter painter(viewport());
		painter.setBrush(palette().color(QPalette::Highlight).lighter(160));
		painter.setPen(Qt::NoPen);
		QTextCursor regionCursor = regionBounds();

		QTextCursor regionStart(document());
		regionStart.setPosition(regionCursor.anchor());
		QTextBlock startBlock = regionStart.block();
		int startLinePos = regionStart.position() - startBlock.position();
		QTextLine startLine = startBlock.layout()->lineForTextPosition(startLinePos);

		QTextCursor regionEnd(document());
		regionEnd.setPosition(regionCursor.position());
		QTextBlock endBlock = regionEnd.block();
		int endLinePos = regionEnd.position() - endBlock.position();
		QTextLine endLine = endBlock.layout()->lineForTextPosition(endLinePos);

		// Draw selection
		qreal top;
		QRectF rect;
		if(startBlock.blockNumber() == endBlock.blockNumber() && startLine.lineNumber() == endLine.lineNumber()) {
			top = blockBoundingGeometry(startBlock).translated(offset).top();
			rect = startLine.naturalTextRect().translated(offset.x() - 0.5, top);
			rect.setLeft(startLine.cursorToX(startLinePos) - 0.5);
			rect.setRight(endLine.cursorToX(endLinePos));
			painter.drawRect(rect);
		} else {
			// Draw selection on start line
			top = blockBoundingGeometry(startBlock).translated(offset).top();
			rect = startLine.naturalTextRect().translated(offset.x() - 0.5, top);
			rect.setLeft(startLine.cursorToX(startLinePos) - 0.5);
			painter.drawRect(rect);

			// Draw selections in between
			QTextBlock block = startBlock;
			int lineNo = startLine.lineNumber() + 1;
			while(!(block.blockNumber() == endBlock.blockNumber() && lineNo == endLine.lineNumber())) {
				if(block.isValid() && lineNo < block.lineCount()) {
					painter.drawRect(block.layout()->lineAt(lineNo).naturalTextRect().translated(offset.x() - 0.5, top));
				}
				++lineNo;
				if(lineNo >= block.lineCount()) {
					block = block.next();
					top = blockBoundingGeometry(block).translated(offset).top();
					lineNo = 0;
				}
			}

			// Draw selection on end line
			top = blockBoundingGeometry(endBlock).translated(offset).top();
			rect = endLine.naturalTextRect().translated(offset.x() - 0.5, top);
			rect.setRight(endLine.cursorToX(endLinePos));
			painter.drawRect(rect);
		}
	}

	QPlainTextEdit::paintEvent(e);

	if(m_drawWhitespace) {
		QTextBlock block = firstVisibleBlock();
		qreal top = blockBoundingGeometry(block).translated(offset).top();
		qreal bottom = top + blockBoundingRect(block).height();

		QPainter painter(viewport());
		painter.setPen(Qt::gray);
		QChar visualArrow((ushort)0x21b5);
		QChar paragraph((ushort)0x00b6);

		// block.next().isValid(): don't draw line break on last block
		while(block.isValid() && block.next().isValid() && top <= e->rect().bottom()) {
			if(block.isVisible() && bottom >= e->rect().top()) {
				QTextLayout* layout = block.layout();
				// Draw hard line breaks (i.e. those not due to word wrapping)
				QTextLine line = layout->lineAt(layout->lineCount() - 1);
				QRectF lineRect = line.naturalTextRect().translated(offset.x(), top);
				if(line.textLength() == 0) {
					painter.drawText(QPointF(lineRect.right(), lineRect.top() + line.ascent()), paragraph);
				} else {
					painter.drawText(QPointF(lineRect.right(), lineRect.top() + line.ascent()), visualArrow);
				}
			}
			block = block.next();
			top = bottom;
			bottom = top + blockBoundingRect(block).height();
		}
	}
}

void OutputTextEdit::saveRegionBounds() {
	QTextCursor c = textCursor();
	if(hasFocus()) {
		bool dorepaint = m_regionCursor.hasSelection() &&
		                 !((m_regionCursor.anchor() == c.anchor() && m_regionCursor.position() == c.position()) ||
		                   (m_regionCursor.anchor() == c.position() && m_regionCursor.position() == c.anchor()));
		m_regionCursor = c;
		if(dorepaint) {
			viewport()->repaint();
		}
		// If only one word is selected, don't treat it as a region
		if(!m_regionCursor.selectedText().contains(QRegExp("\\s"))) {
			m_regionCursor.clearSelection();
		}
	}
	c.movePosition(QTextCursor::Start);
	c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	// If nothing is selected, set the region to the entire contents
	if(!m_regionCursor.hasSelection()) {
		m_regionCursor.setPosition(c.anchor());
		m_regionCursor.setPosition(c.position(), QTextCursor::KeepAnchor);
	}
	m_entireRegion = (m_regionCursor.anchor() == c.anchor() && m_regionCursor.position() == c.position());
}
