#ifndef TRAINSCREEN_H
#define TRAINSCREEN_H

#include <QWidget>
#include <QRectF>
#include <QPointF>
#include <QColor>
#include <QVector>
#include <QStringList>
#include <QTimer>

class QPainter;
class QKeyEvent;
class QMouseEvent;

// 一个车站的静态信息
struct Station
{
    QString     name;       // 中文站名
    QString     en;         // 英文站名
    int         doorSide;   // -1 = 左侧开门, +1 = 右侧开门
    QStringList transfers;  // 换乘线路
    QStringList exits;      // 出入口（左站台 / 右站台 各一个）
    bool        rail = false;  // 是不是国铁车站（闪灯图上挂一个小铁路标志）
};

// 地铁车门上方横条 LCD 模拟（4:1，虚拟画布 1600x400）
//   3 站折返：黄台站 - 望江站 - 云溪站
//   ← → 翻页（每站 4 屏），↑ 向右 / ↓ 向左
class TrainScreen : public QWidget
{
    Q_OBJECT
public:
    explicit TrainScreen(QWidget *parent = nullptr);

    void setState(int step, int station, int dir);
    void setAnimTime(double t) { m_globalT = t; }
    void setTransition(double t);     // 保留空实现（兼容旧参数）
    void primeAnim(double seconds);   // 先跑几秒模拟（出图时让公告栏等动起来）

protected:
    void paintEvent(QPaintEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private slots:
    void tick();

private:
    // 一趟行程的 4 屏（顺序 = 按 → 的顺序；程序一启动就停在"停稳·开门"）
    enum Step {
        StepStopped  = 0,   // 停稳·车门打开（本站）      ← 初始状态
        StepIdle     = 1,   // 车门即将关闭，请注意安全（本站）
        StepMoving   = 2,   // 行驶中（车在两站中间）
        StepArriving = 3    // 即将到站（接近下一站）
    };

    // 每一栏"装的是什么"
    enum PanelType {
        TNone = 0,
        TInfo,              // 左栏信息卡
        TMap,               // 闪灯图
        TDoorClosing,       // 门卡片 · 关门中
        TDoorOpening,       // 门卡片 · 开门方向（下一站哪侧开）
        TDoorOpenStatic,    // 门卡片 · 静态全开
        TLayout             // 站台布局
    };

    enum DoorMode { DoorOpening = 0, DoorClosing = 1, DoorOpenStatic = 2 };

    // 公告栏的一条：文案、左边缘 x、是不是英文
    struct TickerSeg { QString txt; double x; bool en; };

    // ---- 数据 ----
    QVector<Station> m_stations;
    QString m_lineName   = QStringLiteral("龙虾线");
    QString m_lineNameEn = QStringLiteral("LOBSTER METRO");
    QString m_badge      = QStringLiteral("1");
    int    m_curIndex = 0;
    int    m_dir      = 1;
    int    m_step     = StepStopped;

    // ---- 车的移动 ----
    double m_carPos  = 0.0;
    double m_carFrom = 0.0;
    double m_carTo   = 0.0;
    double m_carTime = 1.0;

    // ---- 板块刷新动画（闪烁 + 弹跳）----
    double m_panelT[3]       = { 1, 1, 1 };
    QRectF m_panelFrom[3];
    QRectF m_panelRect[3];
    int    m_panelKey[3]     = { -1, -1, -1 };
    int    m_panelKeyFrom[3] = { -1, -1, -1 };
    int    m_sigStep  = -1;
    int    m_prevStep = -1;

    // ---- 顶栏 / 公告栏 ----
    double m_headT   = 1.0;
    int    m_headSig = -1;
    QVector<TickerSeg> m_tickerSegs;

    // ---- 右上角车厢信息 ----
    int  m_crowd     = 1;       // 拥挤度 1..5（最低就是 1 格，不会到 0）
    bool m_crowdAuto = false;   // 按 Y：到站自动随机
    int  m_carNo     = 1;       // 车厢号 1..8（Tab 切换）
    int  m_acMode    = 0;       // 0 强冷 / 1 弱冷 / 2 制热 / 3 关闭
    int  m_lastIdx   = -1;      // 上一次的站（用来判断"到站了"）
    bool   m_autoPlay = false;  // Enter：自动播报（一段放完 +2 秒 → 自动翻下一屏）
    double m_autoWait = -1.0;

    double m_globalT = 0.0;
    bool   m_paused  = false;
    QTimer m_timer;
    // ---- 逻辑 ----
    int  nextIndex() const;
    bool isTerminal(int idx) const;
    bool validDir(int at, int nd) const;
    const Station &curStation() const;
    const Station &nextStation() const;
    const Station &directionTerminal() const;
    int  showIndex() const;            // 这一屏显示哪个站（前两屏=本站，后两屏=下一站）
    const Station &showStation() const;
    // 这一站是不是【当前行驶方向】的终点站（始发站不箿）
    bool atDirectionTerminus() const;
    double carTarget() const;
    QString statusText() const;
    QString tickerText() const;
    QString tickerTextEn() const;
    QString xferZh() const;      // 换乘提示（没换乘返回空）
    QString xferEn() const;
    void layoutFor(int step, QRectF rect[3], int type[3]) const;
    double infoCardWidth(const Station &st, int mode) const;   // 信息卡"刚好放下内容"需要多宽
    double doorCardWidth(int mode) const;                       // 门卡片"刚好放下内容"需要多宽
    void   mapWindow(int &from, int &to) const;                 // 闪灯图最多显示的 5 个站（滑动窗口）
    QString infoTag(int mode, bool term) const;
    int  contentKey(int step, int col) const;
    int  headSig() const;
    void loadLine();            // 从 assets/line.json 读线路（找不到就用内置默认）
    void nextStep();
    void prevStep();
    void setDirection(int nd);
    void cueForStep();          // 按当前状态放提示音
    void beginPage(int dir, bool carJump = false);

    // ---- 绘制 ----
    void paint(QPainter &p);
    void drawHeader(QPainter &p);
    void drawCabinInfo(QPainter &p);   // 右上角：拥挤度 / 车厢号 / 空调
    void drawPage(QPainter &p, int step);
    void drawColumn(QPainter &p, int col, int step, const QRectF &r);
    void drawInfoCard(QPainter &p, const QRectF &r, int mode);
    void drawDoorCard(QPainter &p, const QRectF &r, int mode);
    void drawRouteMap(QPainter &p, const QRectF &r);
    void drawStationLayout(QPainter &p, const QRectF &r, bool opened);
    void drawTicker(QPainter &p);

    // ---- 小工具 ----
    void   panel(QPainter &p, const QRectF &r, double radius, const QColor &fill);
    void   chevrons(QPainter &p, const QPointF &c, bool pointLeft, double alpha, double s, const QColor &col);
    void   label(QPainter &p, const QString &s, double x, double cy, int px, const QColor &col, bool bold = false);
    // 宽度兜底：画在指定矩形里，超宽就自动缩字号 —— 永远不会压出所属板块
    void   fittedText(QPainter &p, const QString &s, const QRectF &r, int maxPx, int minPx,
                      const QColor &col, bool bold, int flags);
    double pill(QPainter &p, double x, double y, double h, const QString &txt,
                const QColor &fill, const QColor &txtCol, bool outline);
    void   verticalText(QPainter &p, const QString &s, double cx, double cy, int px, const QColor &col);
    void   drawRailMark(QPainter &p, const QPointF &c, double r);   // 小铁路标志（红底圆）
};

#endif // TRAINSCREEN_H
