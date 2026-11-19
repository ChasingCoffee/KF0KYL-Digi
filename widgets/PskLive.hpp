#ifndef PSK_LIVE_HPP__
#define PSK_LIVE_HPP__

// PSK Reporter's live feed: reports of who heard a call, pushed the
// moment they arrive (MQTT at mqtt.pskreporter.info). Only what's needed
// is here: connect, subscribe, keep alive and read the reports.

#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QString>

class QTcpSocket;
class QTimer;

class PskLive : public QObject
{
  Q_OBJECT

public:
  struct Report
  {
    QString receiver;
    QString grid;
    QString band;
    double mhz {0.};
    int snr {0};
    QString mode;
    QDateTime when;
  };

  explicit PskLive (QObject * parent = nullptr);
  ~PskLive ();

  // start listening for reports of this call (empty stops)
  void listen (QString const& call);
  bool connected () const {return subscribed_;}
  QDateTime last_report () const {return last_report_;}

  Q_SIGNAL void report (PskLive::Report const&);
  Q_SIGNAL void state_changed ();

private:
  void open ();
  void close ();
  void read ();
  void send (quint8 type, QByteArray const& body);
  static QByteArray string (QByteArray const&);

  QTcpSocket * socket_;
  QTimer * ping_;
  QTimer * retry_;
  QString call_;
  QByteArray buffer_;
  bool subscribed_ {false};
  int failures_ {0};
  QDateTime last_report_;
};

Q_DECLARE_METATYPE (PskLive::Report)

#endif
