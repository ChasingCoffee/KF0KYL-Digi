#ifndef COLOUR_KEY_STRIP_HPP__
#define COLOUR_KEY_STRIP_HPP__

// What the Band Activity highlight colours mean, always in view beside
// the panel's title. Edit… opens Settings at the Colors tab.

#include <QWidget>
#include <QVector>
#include <QColor>
#include <QRectF>
#include <QString>

#include "models/DecodeHighlightingModel.hpp"

class ColourKeyStrip final
  : public QWidget
{
  Q_OBJECT

public:
  explicit ColourKeyStrip (QWidget * parent = nullptr);

  void set_items (DecodeHighlightingModel::HighlightItems const&);
  QSize sizeHint () const override;
  QSize minimumSizeHint () const override;

  Q_SIGNAL void edit ();
  // a chip double-clicked: the highlight it stands for
  Q_SIGNAL void colour_clicked (DecodeHighlightingModel::Highlight);

protected:
  void paintEvent (QPaintEvent *) override;
  void mouseMoveEvent (QMouseEvent *) override;
  void mouseReleaseEvent (QMouseEvent *) override;
  void mouseDoubleClickEvent (QMouseEvent *) override;
  bool event (QEvent *) override;

private:
  struct Chip {QString text; QColor fg; QColor bg; DecodeHighlightingModel::Highlight type; QRectF rect;};
  static QString short_name (DecodeHighlightingModel::Highlight);
  QVector<Chip> chips_;
  QRectF edit_rect_;
  int hidden_ {0};
};

#endif
