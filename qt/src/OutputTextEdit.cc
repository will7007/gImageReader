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
    QRegularExpression regex = QRegularExpression(searchstr);

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

//    if(selCursor.selectedText().compare(searchstr, cs) == 0) {  // if we are sitting on what we want...
    if(regex.match(selCursor.selectedText(), flags).hasMatch()) {
//        qDebug("Existing match try: %d, %d", selCursor.position(), selCursor.anchor());
        if(replace) {  // then replace it if that's what we want to do
			selCursor.insertText(replacestr);
			selCursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor, replacestr.length());
			setTextCursor(selCursor);
			ensureCursorVisible();
			return true;
            // regex replace is having some trouble
		}
        if(backwards) {  // pick the left-most col so we don't match with outselves again
			selCursor.setPosition(std::min(selCursor.position(), selCursor.anchor()));
        } else {  // pick the right-most char
			selCursor.setPosition(std::max(selCursor.position(), selCursor.anchor()));
//            this causes an issue for .* because we want to match 0+,
//            so we match 0 on the end of the line and cannot loop around.
//            Since nothing has a width of 0, we can look from the right of it and go nowhere,
//            selecting nothing at the end of the line forever (fixed below)
		}
	}
//    qDebug("%s", document()->toRawText().toStdString().c_str());  // could work for extracting text for multi-line regex, but may have lots of overhead

    QTextCursor findcursor = document()->find(regex, selCursor, flags);
//    QTextCursor findcursor = document()->find(searchstr, selCursor, flags);  // result will have the pointer[result]anchor and possibly the reverse for backwards=true
//    qDebug("1st try: %d, %d", findcursor.position(), findcursor.anchor());
//    qDebug("Find cursor: %d, %d", findcursor.position() == findcursor.anchor(), findcursor.atBlockEnd());
    if(findcursor.position() == findcursor.anchor()) {
        if(backwards) {
            if(findcursor.atBlockEnd() && selCursor.anchor() == (findcursor.positionInBlock() + 1)) {
                // need to see if the selCursor anchor == block start because that would mean we shrunk the right/end to nothing while the left/start stayed in place
                // selCursor.anchor().atBlockStart() doesn't exist but we know findcursor was atBlockEnd, i.e. the next block going backwards
                // so previousBlock().getBlockEnd()+1 == start of the next block
                // If we have no found selection (0 chars), we are in regex mode, but we did move (backwards)
                // then most likely we were looking for .* or similar and now we are stuck forever
//                qDebug("Help me! I got stuck going backwards!");
                selCursor.setPosition(std::max(selCursor.position() - 1, regionCursor.anchor()));
                findcursor = document()->find(regex, selCursor, flags);
//                qDebug("1.5 try: %d, %d", findcursor.position(), findcursor.anchor());
            }
        }
        else {
            if(findcursor.atBlockEnd() && selCursor.atBlockEnd()) {
                // If we have no found selection (0 chars), we did not move from the end of the block/line,
                // and we are in regex mode, then most likely we were looking for .* or similar and now we are stuck forever
//                qDebug("Help me! I got stuck going forwards!");
                int newPosition = selCursor.position() + 1;  // try to skip over whatever was giving us trouble by going right of the position, going back to the beginning if we need to
                if(newPosition >= regionCursor.position()) {
                    selCursor.setPosition(regionCursor.anchor());
                }
                else {
                    selCursor.setPosition(newPosition);
                }

//                selCursor.setPosition(std::min(selCursor.position() + 1, regionCursor.position()));
                findcursor = document()->find(regex, selCursor, flags);
//                qDebug("1.5 try: %d, %d", findcursor.position(), findcursor.anchor());
            }
        }
    }

    if(findcursor.isNull() || !(findcursor.anchor() >= regionCursor.anchor() && findcursor.position() <= regionCursor.position())) {  // if we had a bad result or if we were outside of the document somehow
		if(backwards) {
            selCursor.setPosition(regionCursor.position());  // set to the end of the entire text region, similar to the previous setPosition with selCursor
            // the anchor is the left-most thing, the position is the right-most: we put the anchor down as we go forward
		} else {
            selCursor.setPosition(regionCursor.anchor());  // set to the beginning of the entire text region, similar to the previous setPosition with selCursor
		}
//        qDebug("selCursor: %d, %d", selCursor.position(), selCursor.anchor());
        findcursor = document()->find(regex, selCursor, flags);
//        findcursor = document()->find(searchstr, selCursor, flags);  // try searching again
//        qDebug("2nd try: %d, %d", findcursor.position(), findcursor.anchor());
        if(findcursor.isNull() || !(findcursor.anchor() >= regionCursor.anchor() && findcursor.position() <= regionCursor.position())) {  // and give up if we still can't find it
            //selection remains selected when no new match is found: cursor/selection loops around when we can't find anything
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
