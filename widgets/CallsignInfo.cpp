#include "CallsignInfo.hpp"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

#include "moc_CallsignInfo.cpp"

namespace
{
  QPixmap rounded (QPixmap const& source, int photo_size)
  {
    auto const scaled = source.scaled (photo_size, photo_size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap result {photo_size, photo_size};
    result.fill (Qt::transparent);
    QPainter p {&result};
    p.setRenderHint (QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect (0, 0, photo_size, photo_size, 10, 10);
    p.setClipPath (path);
    p.drawPixmap ((photo_size - scaled.width ()) / 2, (photo_size - scaled.height ()) / 2, scaled);
    return result;
  }

  QPixmap initials (QString const& call, int photo_size)
  {
    // placeholder: first letters of the call on a soft tile
    QPixmap result {photo_size, photo_size};
    result.fill (Qt::transparent);
    QPainter p {&result};
    p.setRenderHint (QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect (0, 0, photo_size, photo_size, 10, 10);
    p.fillPath (path, QColor {"#3A6EA5"});
    auto font = p.font ();
    font.setPixelSize (photo_size * 22 / 64);
    font.setBold (true);
    p.setFont (font);
    p.setPen (Qt::white);
    p.drawText (QRect {0, 0, photo_size, photo_size}, Qt::AlignCenter, call.left (2));
    return result;
  }
}

CallsignInfo::CallsignInfo (QWidget * parent)
  : QFrame {parent}
  , photo_ {new QLabel {this}}
  , title_ {new QLabel {this}}
  , details_ {new QLabel {this}}
  , source_ {new QLabel {this}}
  , qrz_button_ {new QPushButton {tr ("QRZ page"), this}}
{
  setObjectName ("callsign_info");
  setFrameShape (QFrame::NoFrame);
  setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Fixed);

  photo_->setFixedSize (photo_size_, photo_size_);
  photo_->setStyleSheet ("background: transparent;");
  title_->setTextFormat (Qt::RichText);
  details_->setTextFormat (Qt::PlainText);
  details_->setWordWrap (true);
  source_->setStyleSheet ("color: palette(mid); font-size: 11px;");
  for (auto * l : {title_, details_, source_})
    {
      l->setTextInteractionFlags (Qt::TextSelectableByMouse);
    }
  qrz_button_->setToolTip (tr ("Open this station's QRZ.com page in your web browser"));
  qrz_button_->setFocusPolicy (Qt::NoFocus);   // never steal the keyboard while operating
  connect (qrz_button_, &QPushButton::clicked, this, &CallsignInfo::openQrzPage);

  // laid out for the DX box in the Conditions window: the photo beside
  // the call, name and place, then the QRZ page button
  title_->setWordWrap (true);
  auto * text = new QVBoxLayout;
  text->setSpacing (1);
  text->addWidget (title_);
  text->addWidget (details_);
  text->addStretch ();

  auto * top = new QHBoxLayout;
  top->setSpacing (12);
  top->addWidget (photo_, 0, Qt::AlignTop);
  top->addLayout (text, 1);

  auto * bottom = new QHBoxLayout;
  bottom->setSpacing (8);
  bottom->addWidget (qrz_button_);
  bottom->addWidget (source_, 1);

  auto * column = new QVBoxLayout {this};
  column->setContentsMargins (0, 0, 0, 0);
  column->setSpacing (8);
  column->addLayout (top);
  column->addLayout (bottom);

  clear ();
}

void CallsignInfo::clear ()
{
  call_.clear ();
  country_.clear ();
  hide ();
}

void CallsignInfo::showCall (QString const& call, QString const& country)
{
  call_ = call.toUpper ();
  country_ = country;
  photo_raw_ = QPixmap {};
  photo_->setPixmap (initials (call_, photo_size_));
  title_->setText (QString {"<span style='font-size:16px; font-weight:600;'>%1</span>"}.arg (call_.toHtmlEscaped ()));
  details_->setText (country_);
  source_->setText (tr ("Looking up…"));
  qrz_button_->setEnabled (true);
  show ();
}

void CallsignInfo::setInfo (CallsignLookup::Info const& i)
{
  if (!call_.contains (i.call)) return;   // a different station is showing now
  auto title = QString {"<span style='font-size:16px; font-weight:600;'>%1</span>"}.arg (call_.toHtmlEscaped ());
  if (i.name.size ())
    {
      title += QString {"&nbsp;&nbsp;<span style='font-size:16px;'>%1</span>"}.arg (i.name.toHtmlEscaped ());
    }
  if (i.nickname.size () && !i.name.contains (i.nickname, Qt::CaseInsensitive))
    {
      title += QString {"&nbsp;<span style='font-size:13px;'>“%1”</span>"}.arg (i.nickname.toHtmlEscaped ());
    }
  title_->setText (title);

  QStringList place;
  if (i.city.size ()) place << i.city;
  if (i.state.size ()) place << i.state;
  place << (i.country.size () ? i.country : country_);
  QStringList line {place.join (", ")};
  if (i.grid.size ()) line << i.grid.left (6);
  if (i.license_class.size ()) line << tr ("%1 class").arg (i.license_class);
  details_->setText (line.join ("  ·  "));
  source_->setText (i.source);
}

void CallsignInfo::setPhoto (QString const& call, QPixmap const& pixmap)
{
  if (!call_.contains (call) || pixmap.isNull ()) return;
  photo_raw_ = pixmap;
  photo_->setPixmap (rounded (pixmap, photo_size_));
}

void CallsignInfo::setNotFound (QString const& call, QString const& reason)
{
  if (!call_.contains (call)) return;
  details_->setText (country_);
  source_->setText (reason);
}

void CallsignInfo::openQrzPage ()
{
  if (call_.isEmpty ()) return;
  QDesktopServices::openUrl (QUrl {QString {"https://www.qrz.com/db/%1"}.arg (QString::fromLatin1 (QUrl::toPercentEncoding (call_)))});
}

void CallsignInfo::setPhotoSize (int size)
{
  if (size == photo_size_) return;
  photo_size_ = size;
  photo_->setFixedSize (size, size);
  if (!photo_raw_.isNull ()) photo_->setPixmap (rounded (photo_raw_, size));
  else if (call_.size ()) photo_->setPixmap (initials (call_, size));
}
