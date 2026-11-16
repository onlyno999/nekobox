#include "NbppNodeList.hpp"

#include <QContextMenuEvent>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <nekobox/dataStore/Configs.hpp>
#include <nekobox/dataStore/Database.hpp>
#include <nekobox/dataStore/ProxyEntity.hpp>
#include <nekobox/global/CountryHelper.hpp>

namespace {

// A clickable node card. No Q_OBJECT: interactions are reported
// through std::function members.
class NbppRow : public QFrame {
public:
  int profileId = -1;
  std::function<void(int)> onClick;
  std::function<void(int)> onDoubleClick;
  std::function<void(int, const QPoint &)> onMenu;

protected:
  void mousePressEvent(QMouseEvent *e) override {
    if (e->button() == Qt::LeftButton && onClick)
      onClick(profileId);
    QFrame::mousePressEvent(e);
  }
  void mouseDoubleClickEvent(QMouseEvent *e) override {
    if (e->button() == Qt::LeftButton && onDoubleClick)
      onDoubleClick(profileId);
    QFrame::mouseDoubleClickEvent(e);
  }
  void contextMenuEvent(QContextMenuEvent *e) override {
    if (onMenu)
      onMenu(profileId, e->globalPos());
  }
};

QString fmtBytes(qint64 b) {
  if (b < 1024)
    return QString::number(b) + " B";
  if (b < 1024 * 1024)
    return QString::number(b / 1024.0, 'f', 1) + " KB";
  if (b < 1024 * 1024 * 1024)
    return QString::number(b / 1048576.0, 'f', 1) + " MB";
  return QString::number(b / 1073741824.0, 'f', 2) + " GB";
}

const char *kCardStyle =
    "background:#ffffff;border:1px solid #f3dbe4;border-radius:12px;";
const char *kCardStartedStyle =
    "background:#fff5f9;border:2px solid #f06292;border-radius:12px;";
const char *kCardSelectedStyle =
    "background:#fffafc;border:2px solid #f8bbd0;border-radius:12px;";

// No drop shadows: a QGraphicsEffect on cards inside a scroll area
// breaks the area's paint optimisation and left unpainted bands.
inline void nbppCardShadow(QWidget *) {}

} // namespace

void NbppNodeList::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.fillRect(rect(), Qt::white);
}

QString nbppCountryNameCN(const QString &cc) {
  static const QMap<QString, QString> names = {
      {"US", "美国"}, {"HK", "香港"}, {"TW", "台湾"}, {"JP", "日本"},
      {"SG", "新加坡"}, {"KR", "韩国"}, {"DE", "德国"}, {"GB", "英国"},
      {"FR", "法国"}, {"NL", "荷兰"}, {"CA", "加拿大"}, {"AU", "澳大利亚"},
      {"RU", "俄罗斯"}, {"IN", "印度"}, {"VN", "越南"}, {"TH", "泰国"},
      {"MY", "马来西亚"}, {"PH", "菲律宾"}, {"ID", "印尼"}, {"TR", "土耳其"},
      {"IT", "意大利"}, {"ES", "西班牙"}, {"CH", "瑞士"}, {"SE", "瑞典"},
      {"PL", "波兰"}, {"UA", "乌克兰"}, {"CN", "中国"}, {"MO", "澳门"},
      {"NZ", "新西兰"}, {"BR", "巴西"}, {"MX", "墨西哥"}, {"AR", "阿根廷"},
      {"CL", "智利"}, {"ZA", "南非"}, {"EG", "埃及"}, {"AE", "阿联酋"},
      {"IL", "以色列"}, {"SA", "沙特"}, {"KZ", "哈萨克斯坦"}, {"NG", "尼日利亚"},
  };
  auto it = names.find(cc);
  return it == names.end() ? cc : it.value();
}

void NbppNodeList::setFilter(const QString &text) {
  if (filterText == text)
    return;
  filterText = text;
  rebuild();
}

NbppNodeList::NbppNodeList(QWidget *parent) : QWidget(parent) {
  // Paint an opaque white background through the palette: plain
  // QWidgets do not honor stylesheet backgrounds, and on a translucent
  // window whatever is behind would show through the gaps.
  auto whiteFill = [](QWidget *w) {
    w->setAutoFillBackground(true);
    QPalette pal = w->palette();
    pal.setColor(QPalette::Window, Qt::white);
    // Scroll viewports paint with Base, not Window — both must be set
    // or the area below the content shows the (dark) palette base.
    pal.setColor(QPalette::Base, Qt::white);
    w->setPalette(pal);
  };
  whiteFill(this);
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  area = new QScrollArea(this);
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  whiteFill(area);
  whiteFill(area->viewport());
  area->setStyleSheet(
      "QScrollBar:vertical{width:8px;background:transparent;}"
      "QScrollBar::handle:vertical{background:#f0c3d6;"
      "border-radius:4px;min-height:30px;}"
      "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical"
      "{height:0;}"
      "QScrollBar:horizontal{height:8px;background:transparent;}"
      "QScrollBar::handle:horizontal{background:#f0c3d6;"
      "border-radius:4px;min-width:30px;}"
      "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal"
      "{width:0;}");
  content = new QWidget(area);
  whiteFill(content);
  lay = new QVBoxLayout(content);
  lay->setContentsMargins(6, 6, 6, 6);
  lay->setSpacing(8);
  lay->addStretch(1);
  area->setWidget(content);
  root->addWidget(area);
}

void NbppNodeList::rebuildThrottled() {
  auto now = QDateTime::currentMSecsSinceEpoch();
  if (now - lastBuildMs < 2000)
    return;
  rebuild();
}

void NbppNodeList::rebuild() {
  lastBuildMs = QDateTime::currentMSecsSinceEpoch();
  int scrollPos = area->verticalScrollBar()->value();

  // Clear previous content (keep the trailing stretch handling simple:
  // delete everything and re-add a stretch at the end).
  QLayoutItem *item;
  while ((item = lay->takeAt(0)) != nullptr) {
    if (item->widget())
      item->widget()->deleteLater();
    delete item;
  }

  auto group = Configs::profileManager->CurrentGroup();
  if (group == nullptr) {
    auto *hint = new QLabel(tr("没有分组"), content);
    hint->setStyleSheet("color:#999;padding:12px;");
    lay->addWidget(hint);
    lay->addStretch(1);
    return;
  }

  const int startedId = Configs::dataStore->started_id;
  const bool armed = group->auto_mode;
  const QString armedCC = group->auto_country;

  // Bucket profiles by region code.
  QMap<QString, QList<std::shared_ptr<Configs::ProxyEntity>>> sections;
  for (int pid : group->profiles) {
    auto ent = Configs::profileManager->GetProfile(pid);
    if (!ent)
      continue;
    if (!filterText.isEmpty() &&
        !ent->name.contains(filterText, Qt::CaseInsensitive))
      continue;
    sections[ent->test_country.toUpper()] << ent;
  }

  if (sections.isEmpty()) {
    auto *hint = new QLabel(
        filterText.isEmpty()
            ? tr("这个分组还没有节点，导入订阅后会显示在这里")
            : tr("没有匹配的节点"),
        content);
    hint->setStyleSheet("color:#999;padding:12px;");
    lay->addWidget(hint);
    lay->addStretch(1);
    area->verticalScrollBar()->setValue(0);
    return;
  }

  // Section order: armed section first, then by code, unknown last.
  QStringList order = sections.keys();
  order.sort();
  if (order.removeAll(""))
    order << "";
  if (armed && !armedCC.isEmpty() && order.contains(armedCC)) {
    order.removeAll(armedCC);
    order.prepend(armedCC);
  }

  for (const QString &cc : order) {
    const auto &ents = sections[cc];
    const bool isArmedSection = armed && armedCC == cc;

    // Sections are expanded by default (the mockup shows the nodes);
    // only an explicit user collapse folds one.
    bool expanded = !userCollapsed.contains(cc);

    // --- section header ---
    auto *header = new QWidget(content);
    header->setStyleSheet(
        "background:#fdf0f4;border-radius:10px;");
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(14, 9, 12, 9);
    QString title;
    if (cc.isEmpty())
      title = QString("？ 未检测 · %1").arg(ents.size());
    else
      title = QString("%1 %2 · %3")
                  .arg(CountryCodeToFlag(cc), nbppCountryNameCN(cc))
                  .arg(ents.size());
    auto *titleBtn = new QPushButton(title, header);
    titleBtn->setFlat(true);
    titleBtn->setStyleSheet(
        "text-align:left;font-weight:bold;font-size:14px;color:#40222e;");
    hl->addWidget(titleBtn, 1);
    auto *armBtn = new QPushButton(isArmedSection ? tr("自动模式") : tr("自动"),
                                   header);
    armBtn->setFlat(true);
    armBtn->setStyleSheet(isArmedSection
                              ? "font-weight:bold;color:#2e7d32;"
                              : "color:#999;");
    hl->addWidget(armBtn);
    auto *arrow = new QLabel(expanded ? "▼" : "▶", header);
    arrow->setStyleSheet("color:#999;");
    hl->addWidget(arrow);
    lay->addWidget(header);

    connect(titleBtn, &QPushButton::clicked, this, [this, cc, expanded] {
      if (expanded) {
        userExpanded.remove(cc);
        userCollapsed.insert(cc);
      } else {
        userCollapsed.remove(cc);
        userExpanded.insert(cc);
      }
      rebuild();
    });
    if (!cc.isEmpty()) {
      connect(armBtn, &QPushButton::clicked, this, [this, group, cc] {
        if (onArmToggle)
          onArmToggle(group->id, cc);
      });
    } else {
      armBtn->setEnabled(false);
    }

    if (!expanded)
      continue;

    // --- node cards ---
    for (auto &ent : ents) {
      const int pid = ent->Id();
      const bool isStarted = pid == startedId;
      const bool inPool =
          armed && (armedCC.isEmpty() || cc == armedCC);
      auto *row = new NbppRow;
      row->setParent(content);
      row->profileId = pid;
      row->setStyleSheet(isStarted ? kCardStartedStyle
                                   : (pid == selectedId ? kCardSelectedStyle
                                                         : kCardStyle));
      nbppCardShadow(row);
      auto *rl = new QHBoxLayout(row);
      rl->setContentsMargins(14, 10, 14, 10);
      rl->setSpacing(8);

      auto *name = new QLabel(ent->name, row);
      name->setStyleSheet("font-weight:bold;font-size:14px;color:#333;");
      rl->addWidget(name);

      auto *chip = new QLabel(ent->type.toUpper(), row);
      chip->setStyleSheet(
          "background:#fde7f0;color:#c2185b;border-radius:6px;"
          "padding:2px 8px;font-size:11px;");
      rl->addWidget(chip);

      QLabel *lat;
      if (ent->latencyInt > 0) {
        lat = new QLabel(QString("%1 ms").arg(ent->latencyInt), row);
        lat->setStyleSheet("color:#2e9e4f;font-weight:bold;");
      } else if (ent->latencyInt < 0) {
        lat = new QLabel(tr("超时"), row);
        lat->setStyleSheet("color:#d32f2f;font-weight:bold;");
      } else {
        lat = new QLabel("—", row);
        lat->setStyleSheet("color:#aaa;");
      }
      rl->addWidget(lat);

      if (ent->traffic_data) {
        auto *tf = new QLabel(
            QString("↑ %1 · ↓ %2")
                .arg(fmtBytes(ent->traffic_data->uplink),
                     fmtBytes(ent->traffic_data->downlink)),
            row);
        tf->setStyleSheet("color:#888;font-size:12px;");
        rl->addWidget(tf);
      }

      rl->addStretch(1);

      if (isStarted) {
        auto *badge = new QLabel(tr("在用"), row);
        badge->setStyleSheet(
            "background:#43a047;color:#fff;border-radius:9px;"
            "padding:3px 12px;font-weight:bold;");
        rl->addWidget(badge);
      } else if (inPool && ent->latencyInt > 0) {
        auto *badge = new QLabel(tr("备选"), row);
        badge->setStyleSheet(
            "background:#eeeeee;color:#777;border-radius:9px;"
            "padding:3px 12px;");
        rl->addWidget(badge);
      }

      row->onClick = [this](int id) {
        selectedId = id;
        rebuild();
      };
      row->onDoubleClick = [this](int id) {
        if (onStartProfile)
          onStartProfile(id);
      };
      row->onMenu = [this](int id, const QPoint &pos) {
        selectedId = id;
        QMenu menu(this);
        auto *actStart = menu.addAction(tr("连接"));
        auto *actStop = menu.addAction(tr("断开"));
        auto *actTest = menu.addAction(tr("测速"));
        auto *actEdit = menu.addAction(tr("编辑"));
        auto *actDel = menu.addAction(tr("删除"));
        actStop->setEnabled(Configs::dataStore->started_id == id);
        auto *chosen = menu.exec(pos);
        if (!chosen)
          return;
        if (chosen == actStart && onStartProfile)
          onStartProfile(id);
        else if (chosen == actStop && onStopProfile)
          onStopProfile();
        else if (chosen == actTest && onTestProfile)
          onTestProfile(id);
        else if (chosen == actEdit && onEditProfile)
          onEditProfile(id);
        else if (chosen == actDel && onDeleteProfile)
          onDeleteProfile(id);
      };

      lay->addWidget(row);
    }
  }

  lay->addStretch(1);
  area->verticalScrollBar()->setValue(scrollPos);
}
