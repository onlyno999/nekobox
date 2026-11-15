#pragma once

#include <QWidget>
#include <QSet>
#include <QString>
#include <functional>

class QScrollArea;
class QVBoxLayout;

// NekoBox++ node list in the approved mockup layout: the current
// group's profiles rendered as cards grouped under collapsible
// region sections. The section header carries the auto-mode toggle
// for that (group, region) pool; the running node shows a green
// "in use" badge and armed-pool members with a latency record show
// a "standby" badge. All actions are delegated to MainWindow
// through the callbacks below.
class NbppNodeList : public QWidget {
public:
  explicit NbppNodeList(QWidget *parent = nullptr);

  void rebuild();
  void rebuildThrottled();
  void setFilter(const QString &text);

  std::function<void(int)> onStartProfile;
  std::function<void()> onStopProfile;
  std::function<void(int)> onTestProfile;
  std::function<void(int)> onEditProfile;
  std::function<void(int)> onDeleteProfile;
  std::function<void(int, const QString &)> onArmToggle;

private:
  QScrollArea *area = nullptr;
  QWidget *content = nullptr;
  QVBoxLayout *lay = nullptr;
  QSet<QString> userCollapsed;
  QSet<QString> userExpanded;
  int selectedId = -1;
  qint64 lastBuildMs = 0;
  QString filterText;
};

// Chinese display name for a two-letter region code (falls back to
// the code itself). Shared with the main window's status row.
QString nbppCountryNameCN(const QString &cc);
