// -*- Mode: C++ -*-
#ifndef DISPLAYTEXT_H
#define DISPLAYTEXT_H

#include <QTextEdit>
#include <QFont>
#include <QHash>
#include <QPair>
#include <QString>
#include <QTimer>
#include <QTextCursor>
#include <QPoint>

class QAction;
class QFrame;
class Configuration;
class LogBook;
class DecodedText;

class DisplayText
  : public QTextEdit
{
  Q_OBJECT
public:
  explicit DisplayText(QWidget *parent = nullptr);
  void set_configuration (Configuration const * configuration, bool high_volume = false)
  {
    disconnect (vertical_scroll_connection_);
    m_config = configuration;
    high_volume_ = high_volume;
  }
  void setContentFont (QFont const&);
  void insertLineSpacer(QString const&);
  void displayDecodedText(DecodedText const& decodedText, QString const& myCall, QString const& mode,
                          bool displayDXCCEntity, LogBook const& logBook,
                          QString const& currentBand=QString {}, bool ppfx=false, bool bCQonly=false,
                          bool haveFSpread = false, float fSpread = 0.0, bool bDisplayPoints=false,
                          int points=-99, QString distance = "", bool alertsMuted=false);
  void displayTransmittedText(QString text, QString modeTx, qint32 txFreq, bool bFastMode,
                              double TRperiod, bool bSuperfox);
  void displayQSY(QString text);
  void displayHoundToBeCalled(QString t, bool bAtTop=false, QColor bg = QColor {}, QColor fg = QColor {});
  void setHighlightedHoundText(QString text);
  void new_period ();
  QString CQPriority(){return m_CQPriority;};
  qint32 m_points;
  bool m_bDisplayPoints;

  Q_SIGNAL void selectCallsign (Qt::KeyboardModifiers);
  Q_SIGNAL void erased ();
  // KF0KYL Digi: single-click line selection
  Q_SIGNAL void line_selected ();      // a line was selected in this window
  Q_SIGNAL void hint_closed ();        // the keyboard hint's x was clicked
  void clear_selection ();
  void set_hint_enabled (bool enabled) {hint_enabled_ = enabled; update_hint ();}
  bool has_selection () const;

  Q_SLOT void insertText (QString const& text, QColor bg = QColor {}, QColor fg = QColor {}
                          , QString const& call1 = QString {}, QString const& call2 = QString {}, QTextCursor::MoveOperation location=QTextCursor::End);
  Q_SLOT void erase ();
  Q_SLOT void highlight_callsign (QString const& callsign, QColor const& bg, QColor const& fg, bool last_period_only);
  // highlight colour of the last decode shown (invalid when not highlighted)
  QColor last_background () const {return last_background_;}

private:
  QColor last_background_;
  void AudioAlerts();
  QTimer alertsTimer;
  QString leftJustifyAppendage (QString message, QString const& appendage) const;
  void mouseDoubleClickEvent (QMouseEvent *) override;
  void mousePressEvent (QMouseEvent *) override;
  void mouseReleaseEvent (QMouseEvent *) override;
  void keyPressEvent (QKeyEvent *) override;
  void paintEvent (QPaintEvent *) override;
  void focusOutEvent (QFocusEvent *) override;
  void select_block (QTextBlock const&);
  void update_hint ();
  void resizeEvent (QResizeEvent *) override;
  QFrame * hint_ {nullptr};
  QTextCursor sel_;
  QPoint press_pos_;
  bool hint_enabled_ {true};

  void extend_vertical_scrollbar (int min, int max);

  Configuration const * m_config;
  bool m_bPrincipalPrefix;
  QString m_CQPriority;
  QString appendWorkedB4(QString message, QString callsign
                         , QString const& grid, QColor * bg, QColor * fg
                         , LogBook const& logBook, QString const& currentBand
                         , QString const& currentMode, QString extra);
  QFont char_font_;
  QAction * erase_action_;

  QHash<QString, QPair<QColor, QColor>> highlighted_calls_;
  bool high_volume_;
  QMetaObject::Connection vertical_scroll_connection_;
  long long modified_vertical_scrollbar_max_;
};

#endif // DISPLAYTEXT_H
