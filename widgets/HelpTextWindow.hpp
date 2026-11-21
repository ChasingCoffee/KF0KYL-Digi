#ifndef HELP_TEXT_WINDOW_HPP__
#define HELP_TEXT_WINDOW_HPP__

#include <QWidget>
#include <QFont>

class QString;

// a window of help text that scrolls when it's taller than the screen
class HelpTextWindow final
  : public QWidget
{
  Q_OBJECT

public:
  HelpTextWindow (QString const& title, QString const& text, QFont const& = QFont {}, QWidget * parent = nullptr);
};

#endif
