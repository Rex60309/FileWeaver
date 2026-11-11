#include "preview_highlighter.hpp"
#include <QColor>
#include <QStringList>
#include <algorithm>
namespace fileweaver::desktop {
PreviewHighlighter::PreviewHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}
void PreviewHighlighter::highlightBlock(const QString &text) {
    const auto limit = std::min(text.size(), qsizetype{4096});
    const QStringList keywords{
        QStringLiteral("if"),     QStringLiteral("else"),     QStringLiteral("for"),
        QStringLiteral("while"),  QStringLiteral("return"),   QStringLiteral("class"),
        QStringLiteral("struct"), QStringLiteral("const"),    QStringLiteral("int"),
        QStringLiteral("void"),   QStringLiteral("auto"),     QStringLiteral("def"),
        QStringLiteral("import"), QStringLiteral("function"), QStringLiteral("true"),
        QStringLiteral("false"),  QStringLiteral("null"),     QStringLiteral("let"),
        QStringLiteral("var"),    QStringLiteral("public"),   QStringLiteral("private")};
    for (qsizetype i = 0; i < limit;) {
        if (text[i] == u'#' || (i + 1 < limit && text[i] == u'/' && text[i + 1] == u'/')) {
            setFormat(static_cast<int>(i), static_cast<int>(limit - i), QColor(90, 150, 90));
            break;
        }
        if (text[i] == u'"' || text[i] == u'\'') {
            const auto quote = text[i];
            const auto begin = i++;
            while (i < limit) {
                if (text[i] == u'\\' && i + 1 < limit) {
                    i += 2;
                    continue;
                }
                if (text[i++] == quote) {
                    break;
                }
            }
            setFormat(static_cast<int>(begin), static_cast<int>(i - begin), QColor(180, 120, 50));
        } else if (text[i].isLetter() || text[i] == u'_') {
            const auto begin = i++;
            while (i < limit && (text[i].isLetterOrNumber() || text[i] == u'_')) {
                ++i;
            }
            if (keywords.contains(text.mid(begin, i - begin))) {
                setFormat(static_cast<int>(begin), static_cast<int>(i - begin),
                          QColor(80, 130, 220));
            }
        } else if (text[i].isDigit()) {
            const auto begin = i++;
            while (i < limit && (text[i].isDigit() || text[i] == u'.')) {
                ++i;
            }
            setFormat(static_cast<int>(begin), static_cast<int>(i - begin), QColor(140, 100, 190));
        } else if (text[i] == u'<' || text[i] == u'>') {
            setFormat(static_cast<int>(i++), 1, QColor(80, 150, 180));
        } else {
            ++i;
        }
    }
}
} // namespace fileweaver::desktop
