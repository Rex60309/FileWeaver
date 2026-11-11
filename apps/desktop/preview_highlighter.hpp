#pragma once
#include <QSyntaxHighlighter>
namespace fileweaver::desktop {
class PreviewHighlighter final : public QSyntaxHighlighter {
  public:
    explicit PreviewHighlighter(QTextDocument *document);

  protected:
    void highlightBlock(const QString &text) override;
};
} // namespace fileweaver::desktop
