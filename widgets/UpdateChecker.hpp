#ifndef UPDATE_CHECKER_HPP__
#define UPDATE_CHECKER_HPP__

// Looks on GitHub for a newer release of KF0KYL Digi and, when there is
// one, offers to download it and start the installer, so nobody has to
// go to the GitHub page.

#include <QObject>
#include <QString>

class QSettings;
class QWidget;
class QNetworkAccessManager;

class UpdateChecker : public QObject
{
  Q_OBJECT

public:
  UpdateChecker (QSettings *, QWidget * window);

  // at start up: quietly, at most once a day, and only says something when
  // there's a new version; from the Help menu: always says what it found
  void check_at_start ();
  void check_now ();

  // "1.2.10" is newer than "1.2.9"
  static bool newer (QString const& theirs, QString const& ours);

private:
  void check (bool manual);
  void offer (QString const& tag, QString const& notes, QString const& page,
              QString const& asset_name, QString const& asset_url, qint64 asset_size);
  void download (QString const& tag, QString const& name, QString const& url, qint64 size);
  void downloaded (QString const& tag, QString const& path);

  QSettings * settings_;
  QWidget * window_;
  QNetworkAccessManager * net_;
  bool busy_ {false};
};

#endif
