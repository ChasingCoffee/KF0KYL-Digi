#include "HelpTextWindow.hpp"

#include <QApplication>
#include <QString>
#include <QPalette>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>

#include "qt_helpers.hpp"

#include "moc_HelpTextWindow.cpp"

HelpTextWindow::HelpTextWindow (QString const& title, QString const& text, QFont const& font, QWidget * parent)
  : QWidget {parent, Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint}
{
  setWindowTitle(QApplication::applicationName () + " - " + title);
  auto * label = new QLabel;
  label->setMargin (10);
  label->setBackgroundRole (QPalette::Base);
  label->setAutoFillBackground (true);
  label->setStyleSheet (font_as_stylesheet (font));
  label->setText (text);
  label->setTextInteractionFlags (Qt::TextSelectableByMouse);
  auto * scroll = new QScrollArea;
  scroll->setWidget (label);
  scroll->setWidgetResizable (true);
  scroll->setFrameShape (QFrame::NoFrame);
  auto * layout = new QVBoxLayout {this};
  layout->setContentsMargins (0, 0, 0, 0);
  layout->addWidget (scroll);
  // as big as the text needs, but never past the screen: it scrolls instead
  QSize want = label->sizeHint () + QSize {24, 4};
  if (auto * screen = QGuiApplication::primaryScreen ())
    {
      auto const room = screen->availableGeometry ().size () * 0.85;
      want = want.boundedTo (room);
    }
  resize (want);
}
