#include "ColourKeyStrip.hpp"

#include <QPainter>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QFontMetricsF>
#include <QEvent>

#include "Win11Theme.hpp"

namespace
{
  double const gap = 5.;
  double const pad = 7.;
}

ColourKeyStrip::ColourKeyStrip (QWidget * parent)
  : QWidget {parent}
{
  setMouseTracking (true);
  setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QString ColourKeyStrip::short_name (DecodeHighlightingModel::Highlight h)
{
  using H = DecodeHighlightingModel::Highlight;
  switch (h)
    {
    case H::CQ: return tr ("CQ");
    case H::MyCall: return tr ("My call");
    case H::Tx: return tr ("Sent");
    case H::DXCC: return tr ("New DXCC");
    case H::DXCCBand: return tr ("New DXCC on band");
    case H::Grid: return tr ("New grid");
    case H::GridBand: return tr ("New grid on band");
    case H::Call: return tr ("New call");
    case H::CallBand: return tr ("New call on band");
    case H::Continent: return tr ("New continent");
    case H::ContinentBand: return tr ("New continent on band");
    case H::CQZone: return tr ("New CQ zone");
    case H::CQZoneBand: return tr ("New CQ zone on band");
    case H::ITUZone: return tr ("New ITU zone");
    case H::ITUZoneBand: return tr ("New ITU zone on band");
    case H::LotW: return tr ("LoTW user");
    }
  return QString {};
}

void ColourKeyStrip::set_items (DecodeHighlightingModel::HighlightItems const& items)
{
  chips_.clear ();
  for (auto const& item : items)
    {
      if (!item.enabled_) continue;
      QColor const bg = Qt::NoBrush != item.background_.style () ? item.background_.color () : QColor {};
      QColor const fg = Qt::NoBrush != item.foreground_.style () ? item.foreground_.color () : QColor {};
      if (!bg.isValid () && !fg.isValid ()) continue;
      chips_ << Chip {short_name (item.type_), fg, bg};
    }
  updateGeometry ();
  update ();
}

QSize ColourKeyStrip::sizeHint () const
{
  QFontMetricsF const fm {font ()};
  double w = fm.horizontalAdvance (tr ("Edit…")) + 8;
  for (auto const& c : chips_) w += fm.horizontalAdvance (c.text) + 2 * pad + gap;
  return QSize {int (w) + 4, int (fm.height ()) + 6};
}

QSize ColourKeyStrip::minimumSizeHint () const
{
  QFontMetricsF const fm {font ()};
  return QSize {int (fm.horizontalAdvance (tr ("Edit…"))) + 12, int (fm.height ()) + 6};
}

void ColourKeyStrip::paintEvent (QPaintEvent *)
{
  QPainter p {this};
  p.setRenderHint (QPainter::Antialiasing);
  QFontMetricsF const fm {font ()};
  double const h = fm.height () + 4;
  double const top = (height () - h) / 2.;
  auto const edit_text = tr ("Edit…");
  double const edit_w = fm.horizontalAdvance (edit_text) + 8;
  bool const dark = Win11Theme::isDark ();
  QColor const plain = dark ? QColor {"#E6EAF0"} : QColor {"#1B1F24"};

  // as many chips as fit, then a count of the rest
  double x = 0.;
  hidden_ = 0;
  for (int i = 0; i < chips_.size (); ++i)
    {
      auto const& c = chips_[i];
      double const w = fm.horizontalAdvance (c.text) + 2 * pad;
      double const more_w = i + 1 < chips_.size () ? fm.horizontalAdvance ("+99") + 2 * pad + gap : 0.;
      if (x + w + more_w + edit_w > width ())
        {
          hidden_ = chips_.size () - i;
          break;
        }
      QRectF const r {x, top, w, h};
      QColor fg = c.fg;
      if (c.bg.isValid ())
        {
          p.setPen (Qt::NoPen);
          p.setBrush (c.bg);
          p.drawRoundedRect (r, 4, 4);
          if (!fg.isValid ()) fg = c.bg.lightness () > 140 ? QColor {"#000000"} : QColor {"#FFFFFF"};
        }
      else
        {
          p.setPen (QPen {QColor {fg.red (), fg.green (), fg.blue (), 140}, 1});
          p.setBrush (Qt::NoBrush);
          p.drawRoundedRect (r.adjusted (0.5, 0.5, -0.5, -0.5), 4, 4);
        }
      auto f = font ();
      f.setBold (true);
      p.setFont (f);
      p.setPen (fg);
      p.drawText (r, Qt::AlignCenter, c.text);
      x += w + gap;
    }
  p.setFont (font ());
  if (hidden_)
    {
      auto const t = QString {"+%1"}.arg (hidden_);
      QRectF const r {x, top, fm.horizontalAdvance (t) + 2 * pad, h};
      p.setPen (QPen {QColor {plain.red (), plain.green (), plain.blue (), 90}, 1});
      p.setBrush (Qt::NoBrush);
      p.drawRoundedRect (r.adjusted (0.5, 0.5, -0.5, -0.5), 4, 4);
      p.setPen (plain);
      p.drawText (r, Qt::AlignCenter, t);
      x += r.width () + gap;
    }
  edit_rect_ = QRectF {x + 2, top, edit_w, h};
  auto f = font ();
  f.setUnderline (underMouse () && edit_rect_.contains (mapFromGlobal (QCursor::pos ())));
  p.setFont (f);
  p.setPen (QColor {Win11Theme::token ("accent")});
  p.drawText (edit_rect_, Qt::AlignVCenter | Qt::AlignLeft, edit_text);
}

void ColourKeyStrip::mouseMoveEvent (QMouseEvent * e)
{
  setCursor (edit_rect_.contains (e->pos ()) ? Qt::PointingHandCursor : Qt::ArrowCursor);
  update ();
}

void ColourKeyStrip::mouseReleaseEvent (QMouseEvent * e)
{
  if (Qt::LeftButton == e->button () && edit_rect_.contains (e->pos ())) Q_EMIT edit ();
}

bool ColourKeyStrip::event (QEvent * e)
{
  if (QEvent::ToolTip == e->type ())
    {
      auto * he = static_cast<QHelpEvent *> (e);
      if (edit_rect_.contains (he->pos ()))
        {
          QToolTip::showText (he->globalPos (), tr ("Change the colours in Settings > Colors"), this);
        }
      else
        {
          // every colour, first one wins when more than one applies
          QString text = tr ("<b>Band Activity colours</b>, top one wins:");
          for (auto const& c : chips_)
            {
              QColor const swatch = c.bg.isValid () ? c.bg : c.fg;
              text += QString {"<br/><span style='background:%1;color:%1'>&nbsp;&nbsp;&nbsp;</span>&nbsp;%2"}.arg (swatch.name (), c.text.toHtmlEscaped ());
            }
          QToolTip::showText (he->globalPos (), text, this);
        }
      return true;
    }
  if (QEvent::Leave == e->type ()) update ();
  return QWidget::event (e);
}
