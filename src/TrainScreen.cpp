#include "TrainScreen.h"
#include "SoundCue.h"

#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QFontMetricsF>
#include <QDateTime>
#include <QString>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRandomGenerator>
#include <cmath>

// ==================================================================
//  亮色主题（地铁报站屏通用做法：白底 + 黑字 + 单一线路色 + 灰阶）
//  虚拟画布 1600 x 400（4:1，车门上方横条）
// ==================================================================
static const double VW = 1600.0;
static const double VH = 400.0;
static const double PI = 3.14159265358979323846;

static const QColor kPage    (255, 255, 255);   // 页面底
static const QColor kCard    (247, 248, 250);   // 卡片底
static const QColor kCard2   (255, 255, 255);   // 卡片内的白块
static const QColor kEdge    (231, 234, 238);   // 边框
static const QColor kDivider (237, 239, 242);   // 分隔线
static const QColor kText    ( 31,  35,  41);   // 主文字
static const QColor kTextSub (138, 144, 153);   // 次要文字
static const QColor kTextWeak(196, 201, 207);   // 更弱（已过站）
static const QColor kLine    (200,  16,  46);   // ★ 线路色（地铁红）
static const QColor kLineSoft(251, 233, 236);   // 线路色浅底
static const QColor kBlue    (  0, 110, 210);   // 信息标签用的蓝字
static const QColor kWarm    (225, 125,  35);   // 制热/提醒用的橙
static const QColor kRail    (225, 229, 233);   // 轨道
static const QColor kRailOld (196, 201, 207);   // 已过段
static const QColor kGlass   (216, 228, 240);   // 门玻璃
static const QColor kBed     (239, 241, 244);   // 轨道床

// 版面
static const QRectF R_HEADER(18, 14, 1564, 54);
static const QRectF R_TICKER(18, 336, 1564, 48);
static const double COL_Y = 80.0, COL_H = 244.0, COL_GAP = 14.0;

// ------------------------------------------------------------------
static QFont fnt(int px, bool bold = false)
{
    QFont f(QStringLiteral("Microsoft YaHei"));
    f.setPixelSize(px);
    f.setBold(bold);
    return f;
}

static QColor withAlpha(const QColor &c, int a) { QColor r = c; r.setAlpha(a); return r; }
static double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
static double lerp(double a, double b, double t) { return a + (b - a) * t; }
static double easeOut(double t) { t = clamp01(t); return 1.0 - std::pow(1.0 - t, 3.0); }

// 更"高级"的非线性曲线
static double easeInOutCubic(double t)
{
    t = clamp01(t);
    return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
}

// 矩形补间：让板块自己滑过去 / 缩放，而不是"旧的消失、新的淡入"
static QRectF lerpRect(const QRectF &a, const QRectF &b, double t)
{
    return QRectF(lerp(a.left(), b.left(), t),  lerp(a.top(), b.top(), t),
                  lerp(a.width(), b.width(), t), lerp(a.height(), b.height(), t));
}

// 弹跳曲线：先冲过头一点点，再回落到 1（"弹一下"的感觉）
static double popScale(double t)
{
    t = clamp01(t);
    if (t < 0.42) return lerp(0.93, 1.025, easeOut(t / 0.42));
    return lerp(1.025, 1.0, easeOut((t - 0.42) / 0.58));
}

static double pseudo(int i)
{
    double x = std::sin(i * 12.9898 + 78.233) * 43758.5453;
    return x - std::floor(x);
}

static double baselineY(const QRectF &r, const QFontMetricsF &fm)
{
    return r.center().y() + (fm.ascent() - fm.descent()) / 2.0;
}

// 开门动画：0 → 全开（缓入缓出，慢一点别弹），然后【直接跳回】0 再来一遍
static double doorOpen(double t, double cycle = 1.9)
{
    const double p = std::fmod(t, cycle) / cycle;
    return easeInOutCubic(p);
}

// 关门动画：全开 → 全关（开门动作的镜像）
static double doorClose(double t, double cycle = 1.9)
{
    return 1.0 - doorOpen(t, cycle);
}

// 换乘标识用对方线路的颜色（真实地铁的做法）
static QColor lineColorOf(const QString &line)
{
    if (line.contains(QStringLiteral("2"))) return QColor(  0,  85, 164);  // 2号线 蓝
    if (line.contains(QStringLiteral("3"))) return QColor(210, 151,   0);  // 3号线 黄
    if (line.contains(QStringLiteral("4"))) return QColor(  0, 142, 156);  // 4号线 青
    return kLine;
}

// ==================================================================
//  构造 / 数据
// ==================================================================
TrainScreen::TrainScreen(QWidget *parent)
    : QWidget(parent)
{
    // 线路从 assets/line.json 读；想设计自己的线路只改那个文件就行
    loadLine();
    if (m_stations.size() < 2) {
        // 没配置文件 / 配置不合法 → 用内置默认（黄台 - 望江 - 云溪）
        m_stations.clear();
        m_stations.append(Station{ QStringLiteral("黄台站"), QStringLiteral("Huangtai"),
                                   -1, { QStringLiteral("2号线") }, { QStringLiteral("A"), QStringLiteral("B") } });
        m_stations.append(Station{ QStringLiteral("望江站"), QStringLiteral("Wangjiang"),
                                   +1, { QStringLiteral("4号线") }, { QStringLiteral("C"), QStringLiteral("D") } });
        m_stations.append(Station{ QStringLiteral("云溪站"), QStringLiteral("Yunxi"),
                                   -1, { QStringLiteral("3号线") }, { QStringLiteral("E"), QStringLiteral("F") } });
    }

    setMinimumSize(640, 160);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &TrainScreen::tick);
    m_timer.start(16);
}

// 读 assets/line.json —— 设计自己的线路只需要改那个文件
void TrainScreen::loadLine()
{
    const QString base = QCoreApplication::applicationDirPath();
    const QStringList cand = {
        base + QLatin1String("/assets/line.json"),
        base + QLatin1String("/../assets/line.json"),
        base + QLatin1String("/../../assets/line.json"),
        base + QLatin1String("/../../../assets/line.json"),
        QDir::currentPath() + QLatin1String("/assets/line.json"),
    };
    QString path;
    for (const QString &c : cand) if (QFileInfo::exists(c)) { path = c; break; }
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    const QJsonObject o = doc.object();

    m_lineName   = o.value(QStringLiteral("lineName")).toString(m_lineName);
    m_lineNameEn = o.value(QStringLiteral("lineNameEn")).toString(m_lineNameEn);
    m_badge      = o.value(QStringLiteral("badge")).toString(m_badge);

    QVector<Station> list;
    const QJsonArray arr = o.value(QStringLiteral("stations")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject s = v.toObject();
        Station st;
        st.name = s.value(QStringLiteral("name")).toString();
        st.en   = s.value(QStringLiteral("en")).toString();
        st.doorSide = s.value(QStringLiteral("door")).toString().toLower()
                      == QLatin1String("right") ? +1 : -1;
        st.rail = s.value(QStringLiteral("rail")).toBool(false);
        for (const QJsonValue &t : s.value(QStringLiteral("transfers")).toArray())
            st.transfers << t.toString();
        for (const QJsonValue &x : s.value(QStringLiteral("exits")).toArray())
            st.exits << x.toString();
        if (!st.name.isEmpty()) list << st;
    }
    if (list.size() >= 2) m_stations = list;
}

void TrainScreen::setState(int step, int station, int dir)
{
    m_curIndex = qBound(0, station, m_stations.size() - 1);
    m_dir = (dir < 0) ? -1 : 1;
    if (!validDir(m_curIndex, m_dir)) m_dir = -m_dir;
    m_step = qBound(0, step, 3);
    m_carPos = carTarget();
    m_carFrom = m_carTo = m_carPos;
    m_carTime = 1.0;
    m_headT = 1.0;
    m_headSig = headSig();
    m_sigStep = m_step;
    m_prevStep = -1;
    QRectF rtS[3]; int ttS[3];
    layoutFor(m_step, rtS, ttS);
    for (int i = 0; i < 3; ++i) {
        m_panelKey[i] = m_panelKeyFrom[i] = contentKey(m_step, i);
        m_panelRect[i] = rtS[i];
        m_panelT[i] = 1.0;
    }
    Q_UNUSED(ttS)
}

// 先跑一段时间的模拟（直接推进内部状态），出图时用
void TrainScreen::primeAnim(double seconds)
{
    const int n = int(seconds / 0.016);
    for (int i = 0; i < n; ++i) tick();
    m_globalT = 0.55;
}

void TrainScreen::setTransition(double t)
{
    // 仅出图用：把过渡冻结在指定进度，好检查有没有抽动
    m_prevStep = (m_step > 0) ? m_step - 1 : 0;
    QRectF rf[3]; int tf[3];
    layoutFor(m_prevStep, rf, tf);
    const double v = clamp01(t);
    for (int i = 0; i < 3; ++i) {
        const int kc = contentKey(m_step, i);
        const int kp = contentKey(m_prevStep, i);
        m_panelKey[i]     = kc;
        m_panelKeyFrom[i] = kp;
        m_panelFrom[i]    = rf[i];
        m_panelT[i]       = (kc == kp) ? 1.0 : v;   // 内容没变的栏不参与动画
    }
}

// ==================================================================
//  逻辑
// ==================================================================
int TrainScreen::nextIndex() const
{
    return qBound(0, m_curIndex + m_dir, m_stations.size() - 1);
}

bool TrainScreen::isTerminal(int idx) const
{
    return idx <= 0 || idx >= m_stations.size() - 1;
}

bool TrainScreen::validDir(int at, int nd) const
{
    const int t = at + nd;
    return t >= 0 && t < m_stations.size();
}

const Station &TrainScreen::curStation() const  { return m_stations.at(m_curIndex); }
const Station &TrainScreen::nextStation() const { return m_stations.at(nextIndex()); }
const Station &TrainScreen::directionTerminal() const
{
    return m_dir > 0 ? m_stations.last() : m_stations.first();
}

// 这一屏该显示哪个站：
//   停稳·开门 / 即将关门 → 本站（车就停在这）
//   行驶中 / 即将到站   → 下一站
int TrainScreen::showIndex() const
{
    return (m_step <= StepIdle) ? m_curIndex : nextIndex();
}

const Station &TrainScreen::showStation() const
{
    return m_stations.at(showIndex());
}

// 这一站是不是【当前行驶方向】的终点站？
// 注意：线路两端的「终点站」只有在“开过去”的时候才算；
// 始发站（刚开门准备发车）不算终点站，否则会报成"终点站到了，请所有乘客下车"
bool TrainScreen::atDirectionTerminus() const
{
    const int t = (m_dir > 0) ? m_stations.size() - 1 : 0;
    return showIndex() == t;
}

double TrainScreen::carTarget() const
{
    switch (m_step) {
    case StepMoving:   return 0.5;
    case StepArriving: return 0.88;
    default:           return 0.0;     // 停稳·开门 / 即将关门：车就停在站上
    }
}

QString TrainScreen::statusText() const
{
    const Station &n = showStation();
    switch (m_step) {
    case StepMoving:
        return QStringLiteral("行驶中 · 前往 %1").arg(n.name);
    case StepArriving:
        return atDirectionTerminus()
                ? QStringLiteral("即将到站 · %1（终点站）").arg(n.name)
                : QStringLiteral("即将到站 · %1").arg(n.name);
    case StepStopped:
        return atDirectionTerminus()
                ? QStringLiteral("已经到站 · 终点站 %1").arg(n.name)
                : QStringLiteral("本站 %1 · 车门已开启").arg(n.name);
    default:
        return QStringLiteral("车门即将关闭，请注意安全");
    }
}

// 换乘提示（没有换乘就返回空串）
QString TrainScreen::xferZh() const
{
    const QStringList &t = showStation().transfers;
    return t.isEmpty() ? QString() : QStringLiteral("可换乘%1。").arg(t.join(QStringLiteral("、")));
}

QString TrainScreen::xferEn() const
{
    const QStringList &t = showStation().transfers;
    if (t.isEmpty()) return QString();
    QStringList l;
    for (const QString &s : t)
        l << (QStringLiteral("Line ") + QString(s).remove(QStringLiteral("号线")));
    return QStringLiteral("Transfer to %1.").arg(l.join(QStringLiteral(", ")));
}

QString TrainScreen::tickerText() const
{
    const Station &n = showStation();
    const QString side = (n.doorSide < 0) ? QStringLiteral("左侧") : QStringLiteral("右侧");

    switch (m_step) {
    case StepMoving:
        return QStringLiteral("列车正在行驶，前方到站 %1，请站稳扶好，%2车门将会打开。").arg(n.name, side);
    case StepArriving:
        return atDirectionTerminus()
                ? QStringLiteral("列车即将到达终点站 %1，请所有乘客带好随身物品，准备下车。").arg(n.name)
                : QStringLiteral("列车即将到达 %1，%2车门将会打开，请提前做好下车准备。").arg(n.name, side) + xferZh();
    case StepStopped:
        return atDirectionTerminus()
                ? QStringLiteral("终点站 %1 到了，请所有乘客下车，感谢乘坐%2。").arg(n.name, m_lineName)
                : QStringLiteral("本站 %1，请从%2车门下车，先下后上，注意列车与站台之间的空隙。").arg(n.name, side) + xferZh();
    default:
        return QStringLiteral("车门即将关闭，请注意安全，请勿抢上抢下，感谢配合。");
    }
}

// 英文版公告（公告栏里中英交替）
QString TrainScreen::tickerTextEn() const
{
    const Station &n = showStation();
    const bool left = (n.doorSide < 0);
    const QString sideLow = left ? QStringLiteral("left") : QStringLiteral("right");
    const QString sideCap = left ? QStringLiteral("Left") : QStringLiteral("Right");

    switch (m_step) {
    case StepMoving:
        return QStringLiteral("This train is now running. Next station: %1. %2-side doors will open.")
                .arg(n.en, sideCap);
    case StepArriving:
        return atDirectionTerminus()
                ? QStringLiteral("Arriving at the terminus, %1. Please take all your belongings and get ready to alight.").arg(n.en)
                : QStringLiteral("Arriving at %1. %2-side doors will open. Please get ready to alight.").arg(n.en, sideCap) + QStringLiteral(" ") + xferEn();
    case StepStopped:
        return atDirectionTerminus()
                ? QStringLiteral("Terminus, %1. Please alight from the %2-side doors. Thank you for travelling with Metro.").arg(n.en, sideLow)
                : QStringLiteral("This is %1. Please alight from the %2-side doors, and mind the gap between the train and the platform.").arg(n.en, sideLow) + QStringLiteral(" ") + xferEn();
    default:
        return QStringLiteral("Doors are closing. Please mind your safety and keep clear of the doors.");
    }
}

// 胶囊宽度（跟 pill() 里的算法保持一致）
static double pillW(double h, const QString &txt)
{
    const QFontMetricsF fm(fnt(int(h * 0.54), true));
    return fm.horizontalAdvance(txt) + h * 1.15;
}

// 信息卡的标签文字
QString TrainScreen::infoTag(int mode, bool term) const
{
    if (mode == 2)      return term ? QStringLiteral("终点站 · TERMINUS") : QStringLiteral("本站 · THIS STATION");
    else if (mode == 1) return term ? QStringLiteral("即将到站 · 终点站")   : QStringLiteral("即将到站 · ARRIVING");
    return QStringLiteral("下一站 · NEXT");
}

// 信息卡"刚好放下内容"需要多宽
// （取"标签 / 站名 / 英文名 / 换乘行 / 出入口行"里最宽的那一行）
double TrainScreen::infoCardWidth(const Station &st, int mode) const
{
    double w = pillW(30, infoTag(mode, atDirectionTerminus()));
    w = qMax(w, QFontMetricsF(fnt(40, true)).horizontalAdvance(st.name));
    w = qMax(w, QFontMetricsF(fnt(14)).horizontalAdvance(st.en));
    double row = 0;
    for (const QString &t : st.transfers) row += pillW(30, t);
    w = qMax(w, row);
    row = 0;
    for (const QString &x : st.exits) row += pillW(30, QStringLiteral("出口 ") + x);
    w = qMax(w, row);
    return qBound(190.0, w + 40.0, 380.0);
}

// 门卡片"刚好放下内容"需要多宽
double TrainScreen::doorCardWidth(int mode) const
{
    double w = 96.0;   // 门图形本身
    const QFontMetricsF big(fnt(26, true));
    w = qMax(w, big.horizontalAdvance(mode == DoorClosing ? QStringLiteral("车门即将关闭")
                                                          : QStringLiteral("右侧开门")));
    w = qMax(w, QFontMetricsF(fnt(12)).horizontalAdvance(QStringLiteral("开门方向 · DOORS")));
    w = qMax(w, QFontMetricsF(fnt(13)).horizontalAdvance(QStringLiteral("请注意安全 · 请勿抢上抢下")));
    return qBound(150.0, w + 34.0, 320.0);
}

// 闪灯图最多同时显示 5 个站：以"这一屏显示的站"为中心，靠边就往另一边补
void TrainScreen::mapWindow(int &from, int &to) const
{
    const int n = m_stations.size();
    // 只有"即将到站"这一屏收窄（最多 6 个站）；
    // 其余屏（即将关门 / 行驶中）显示【完整线路图】
    const int maxN = (m_step == StepArriving) ? 6 : n;
    if (n <= maxN) { from = 0; to = n - 1; return; }

    const int center = showIndex();
    const int half   = maxN / 2;
    from = center - (half - 1);      // 6 个站：前面 2 个
    to   = center + half;            //         后面 3 个
    if (from < 0)   { to += -from;  from = 0; }
    if (to > n - 1) { from -= (to - (n - 1)); to = n - 1; }
    from = qMax(0, from);
    to   = qMin(n - 1, to);
}

// 每一屏三栏的"矩形 + 内容类型" —— 唯一真源，画的时候也用它
// 左右两栏按内容自动收窄，把空间让给中间的闪灯图
void TrainScreen::layoutFor(int step, QRectF rect[3], int type[3]) const
{
    const double left = 18.0, g = COL_GAP, total = 1564.0;
    const int infoMode = (step == StepArriving) ? 1
                       : ((step == StepStopped || step == StepIdle) ? 2 : 0);
    const double w0 = infoCardWidth(showStation(), infoMode);
    double w1 = 0.0, w2 = 0.0;

    if (step == StepStopped) {
        // 停稳·开门：信息 | 站台布局（宽） | 门（静态开）
        w2 = doorCardWidth(DoorOpenStatic);
        w1 = total - w0 - w2 - 2 * g;
        type[1] = TLayout;   type[2] = TDoorOpenStatic;
    } else if (step == StepArriving) {
        // 即将到站：信息（窄） | 闪灯图 | 站台布局
        w1 = 520.0;
        w2 = total - w0 - w1 - 2 * g;
        type[1] = TMap;      type[2] = TLayout;
    } else {
        // 即将关门 / 行驶中：信息（窄） | 闪灯图（宽） | 门（窄）
        w2 = doorCardWidth(step == StepIdle ? DoorClosing : DoorOpening);
        w1 = total - w0 - w2 - 2 * g;
        type[1] = TMap;
        type[2] = (step == StepIdle) ? TDoorClosing : TDoorOpening;
    }

    type[0] = TInfo;
    rect[0] = QRectF(left, COL_Y, w0, COL_H);
    rect[1] = QRectF(left + w0 + g, COL_Y, w1, COL_H);
    rect[2] = QRectF(left + w0 + g + w1 + g, COL_Y, w2, COL_H);
}

// 每一栏的"内容标识"：类型 + 具体状态。标识一样、矩形也没变 → 这一栏不刷新
int TrainScreen::contentKey(int step, int col) const
{
    QRectF rt[3]; int tt[3];
    layoutFor(step, rt, tt);
    switch (tt[col]) {
    case TInfo:           return 100 + (step == StepArriving ? 1 : ((step == StepStopped || step == StepIdle) ? 2 : 0));
    case TMap:            return 200;
    case TDoorClosing:    return 301;
    case TDoorOpening:    return 302;
    case TDoorOpenStatic: return 303;
    case TLayout:         return 400 + (step == StepStopped ? 1 : 0);
    default:              return 0;
    }
}

// 画整页（三栏按这一屏的版式）
void TrainScreen::drawPage(QPainter &p, int step)
{
    QRectF rt[3]; int tt[3];
    layoutFor(step, rt, tt);
    for (int col = 0; col < 3; ++col) drawColumn(p, col, step, rt[col]);
}

int TrainScreen::headSig() const
{
    const QString s = statusText();
    int h = 0;
    for (int i = 0; i < s.size(); ++i) h = h * 131 + s.at(i).unicode();
    return h;
}

// 翻页：方向 + 车的位移补间。
// 栏的入场动画不在里触发 —— 由 tick() 比对指纹自动决定
void TrainScreen::beginPage(int dir, bool carJump)
{
    Q_UNUSED(dir)   // 现在的过渡是"按板块淡入淡出"，不需要翻页方向

    if (carJump) {
        // 换了一段行程，车的相对位置直接归位（不做跨段滑动）
        m_carPos = carTarget();
        m_carFrom = m_carTo = m_carPos;
        m_carTime = 1.0;
    } else {
        m_carFrom = m_carPos;
        m_carTo = carTarget();
        m_carTime = 0.0;
    }
}

// 按 (栏, 屏) 画出这一栏的内容
void TrainScreen::drawColumn(QPainter &p, int col, int step, const QRectF &r)
{
    QRectF rt[3]; int tt[3];
    layoutFor(step, rt, tt);
    switch (tt[col]) {
    case TInfo:
        drawInfoCard(p, r, step == StepArriving ? 1 : ((step == StepStopped || step == StepIdle) ? 2 : 0));
        break;
    case TMap:
        drawRouteMap(p, r);
        break;
    case TDoorClosing:    drawDoorCard(p, r, DoorClosing);    break;
    case TDoorOpening:    drawDoorCard(p, r, DoorOpening);    break;
    case TDoorOpenStatic: drawDoorCard(p, r, DoorOpenStatic); break;
    case TLayout:
        drawStationLayout(p, r, step == StepStopped);
        break;
    default:
        break;
    }
}

// → 一屏一屏往下走：停稳开门 → 即将关门 → 行驶中 → 即将到站 → 下一站的停稳开门
void TrainScreen::nextStep()
{
    if (m_step < StepArriving) {
        // 在"即将关门"这一屏，如果前方已经没有站（线路尽头）→ 停住不让走
        if (m_step == StepIdle && !validDir(m_curIndex, m_dir)) return;
        ++m_step;
        beginPage(+1);
        cueForStep();
        return;
    }

    // 已经在"即将到站"：列车进站 → 开进下一站，停在"停稳·开门"
    const int t = m_curIndex + m_dir;
    if (t < 0 || t >= m_stations.size()) return;
    m_curIndex = t;
    m_step = StepStopped;
    beginPage(+1, true);
    cueForStep();
}

// ← 反向翻回去；在"停稳·开门"再按 ← 就退回上一站
void TrainScreen::prevStep()
{
    if (m_step > StepStopped) { --m_step; beginPage(-1); cueForStep(); return; }

    const int p = m_curIndex - m_dir;
    if (p < 0 || p >= m_stations.size()) return;
    m_curIndex = p;
    m_step = StepArriving;      // 退回"即将到站"那一屏
    beginPage(-1, true);
    cueForStep();
}

// ↑ = 向右，↓ = 向左；换向后画面回到"停稳·开门"那一屏
void TrainScreen::setDirection(int nd)
{
    const int at = (m_step >= StepMoving) ? nextIndex() : m_curIndex;
    if (!validDir(at, nd)) return;
    m_curIndex = at;
    m_dir = nd;
    m_step = StepStopped;
    beginPage(nd, true);
    cueForStep();
}

// 按当前屏放声音（挂在按键处理里，不是绘制里）
//   切页先把上一段掐掉；
//   ★ 换乘站：在"行驶中 / 即将到站"先用南京提示音打底，响完再报站（顺序播）
//   即将关门 = 关门铃 + 关门广播；其他屏按站名报站（中英各一条）
void TrainScreen::cueForStep()
{
    SoundCue::stopAll();                       // 切页即静音

    const bool xfer = !showStation().transfers.isEmpty();
    const QString num = QStringLiteral("%1").arg(showIndex() + 1, 2, 10, QChar('0'));
    const QString W = QStringLiteral(".wav");

    switch (m_step) {
    case StepStopped: {
        // 按【当前行驶方向】判断是不是终点站吃：是→终点站报站；在端点站换向后开门=始发站→普通报站
        const QString pre = atDirectionTerminus() ? QStringLiteral("term_") : QStringLiteral("stop_");
        SoundCue::playSeq({ pre + QStringLiteral("zh_") + num + W,
                            pre + QStringLiteral("en_") + num + W });
        break; }

    case StepIdle:
        // 先播关门语音，再响关门铃（按飞云要求）
        SoundCue::playSeq({ QStringLiteral("close_zh.wav"),
                            QStringLiteral("close_en.wav"),
                            QStringLiteral("door_close.wav") });
        break;

    case StepMoving: {
        QStringList q;
        if (xfer) q << QStringLiteral("nanjing_prompt.wav");   // 换乘站：先响提示音
        q << QStringLiteral("run_zh_") + num + W
          << QStringLiteral("run_en_") + num + W;
        SoundCue::playSeq(q);
        break; }

    case StepArriving: {
        QStringList q;
        if (xfer) q << QStringLiteral("nanjing_prompt.wav");   // 换乘站：先响提示音
        q << QStringLiteral("arr_zh_") + num + W
          << QStringLiteral("arr_en_") + num + W;
        SoundCue::playSeq(q);
        break; }

    default:
        break;
    }
}

// ==================================================================
//  事件 / 定时
// ==================================================================
void TrainScreen::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Right:  nextStep();      break;
    case Qt::Key_Left:   prevStep();      break;
    case Qt::Key_Up:     setDirection(+1); break;
    case Qt::Key_Down:   setDirection(-1); break;
    case Qt::Key_Y:
        m_crowdAuto = !m_crowdAuto;
        m_lastIdx = m_curIndex;      // 开关时不立刻跳，等下一站再变
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        if (m_crowd < 5) ++m_crowd;
        break;
    case Qt::Key_Minus:
        if (m_crowd > 1) --m_crowd;
        break;
    case Qt::Key_Tab:
        m_carNo = (m_carNo % 8) + 1;     // 1..8 轮回
        break;
    case Qt::Key_C:
        m_acMode = (m_acMode == 0) ? 1 : 0;   // 强冷 ↔ 弱冷
        break;
    case Qt::Key_H:
        m_acMode = 2;                    // 制热
        break;
    case Qt::Key_O:
        m_acMode = 3;                    // 关闭
        break;
    case Qt::Key_S:
        SoundCue::enabled = !SoundCue::enabled;
        if (!SoundCue::enabled) SoundCue::stopAll();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        m_autoPlay = !m_autoPlay;         // Enter：自动播报开关
        m_autoWait = -1.0;
        if (!m_autoPlay) SoundCue::stopAll();
        break;
    case Qt::Key_Space:  m_paused = !m_paused; break;
    case Qt::Key_F:
    case Qt::Key_F11:
        if (isFullScreen()) showNormal(); else showFullScreen();
        break;
    case Qt::Key_Escape:
        if (isFullScreen()) showNormal(); else close();
        break;
    default:
        QWidget::keyPressEvent(e);
    }
}

void TrainScreen::mouseDoubleClickEvent(QMouseEvent *)
{
    if (isFullScreen()) showNormal(); else showFullScreen();
}

void TrainScreen::tick()
{
    const double dt = 0.016;
    if (!m_paused) {
        m_globalT += dt;

        // 页面换了 → 记下上一页
        if (m_step != m_sigStep) { m_prevStep = m_sigStep; m_sigStep = m_step; }

        QRectF rt[3]; int tt[3]; layoutFor(m_step, rt, tt);
        QRectF rf[3]; int tf[3]; layoutFor(m_prevStep < 0 ? m_step : m_prevStep, rf, tf);

        // 只有"卡片里的内容变了"才播动画；只是位置/尺寸变了 → 立即生效（不做放大缩小）
        for (int i = 0; i < 3; ++i) {
            const int k = contentKey(m_step, i);
            if (k != m_panelKey[i]) {
                m_panelKeyFrom[i] = (m_prevStep < 0) ? k : contentKey(m_prevStep, i);
                m_panelKey[i] = k;
                m_panelFrom[i] = rf[i];
                m_panelRect[i] = rt[i];
                m_panelT[i] = 0.0;
            }
            if (m_panelT[i] < 1.0) m_panelT[i] = qMin(1.0, m_panelT[i] + dt / 0.34);
        }

        // 公告栏：一条连续的"流"，中文、英文、中文、英文……交替
        {
            const double gap = 180.0, clipL = 102.0, clipR = 1572.0;
            const QFontMetricsF tfm(fnt(19));

            for (int i = 0; i < m_tickerSegs.size(); ++i)
                m_tickerSegs[i].x -= 105.0 * dt;

            while (!m_tickerSegs.isEmpty() &&
                   m_tickerSegs.first().x + tfm.horizontalAdvance(m_tickerSegs.first().txt) < clipL)
                m_tickerSegs.removeFirst();

            // 还没进场的那条，内容换成最新的（中英各自对应）
            if (!m_tickerSegs.isEmpty() && m_tickerSegs.last().x >= clipR)
                m_tickerSegs.last().txt = m_tickerSegs.last().en ? tickerTextEn() : tickerText();

            double tail = clipR;
            if (!m_tickerSegs.isEmpty()) {
                const TickerSeg &last = m_tickerSegs.last();
                tail = last.x + tfm.horizontalAdvance(last.txt) + gap;
            }
            if (tail <= clipR) {
                const bool en = m_tickerSegs.isEmpty() ? false : !m_tickerSegs.last().en;
                TickerSeg s;
                s.en  = en;
                s.txt = en ? tickerTextEn() : tickerText();
                s.x   = qMax(clipR, tail);
                m_tickerSegs.append(s);
            }
        }

        // 到站了 & 拥挤度自动模式：
        //   终点站 → 清到最低（人全下车了）
        //   中间站 → 随机 ±2 格
        if (m_crowdAuto && m_curIndex != m_lastIdx) {
            m_lastIdx = m_curIndex;
            if (isTerminal(m_curIndex)) {
                m_crowd = 1;
            } else {
                const int d = QRandomGenerator::global()->bounded(-2, 3);   // -2..+2
                m_crowd = qBound(1, m_crowd + d, 5);
            }
        }

        // Enter 自动播报：一段播完 → 等 2 秒 → 自动翻下一屏
        if (m_autoPlay) {
            if (SoundCue::remainingSeconds() <= 0.01) {
                if (m_autoWait < 0.0) {
                    m_autoWait = 2.0;
                } else {
                    m_autoWait -= dt;
                    if (m_autoWait <= 0.0) {
                        m_autoWait = -1.0;
                        const int st0 = m_step, ci0 = m_curIndex;
                        nextStep();
                        if (m_step == st0 && m_curIndex == ci0)   // 到线路尽头 → 掉头继续
                            setDirection(-m_dir);
                    }
                }
            } else {
                m_autoWait = -1.0;
            }
        }

        const int hs = headSig();
        if (hs != m_headSig) { m_headSig = hs; m_headT = 0.0; }
        if (m_headT < 1.0) m_headT = qMin(1.0, m_headT + dt / 0.34);

        // 车的移动（cubic 缓入缓出，0.5 秒）
        if (m_carTime < 1.0) {
            m_carTime = qMin(1.0, m_carTime + dt / 0.50);
            m_carPos = lerp(m_carFrom, m_carTo, easeInOutCubic(m_carTime));
        }
    }
    update();
}

// ==================================================================
//  主绘制
// ==================================================================
void TrainScreen::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const double w = width(), h = height();
    p.fillRect(QRectF(0, 0, w, h), kPage);

    const double s = qMin(w / VW, h / VH);
    p.save();
    p.translate((w - VW * s) / 2.0, (h - VH * s) / 2.0);
    p.scale(s, s);
    paint(p);
    p.restore();
}

void TrainScreen::paint(QPainter &p)
{
    // 页面底 + 顶部线路色条（地铁味）
    p.fillRect(QRectF(0, 0, VW, VH), kPage);
    p.setPen(Qt::NoPen);
    p.setBrush(kLine);
    p.drawRect(QRectF(0, 0, VW, 4));

    drawHeader(p);

    // 只在【卡片里边的元素】上做非线性动画：
    // 卡片外框不动、不缩放、不闪 —— 旧内容往左滑出去淡出，新内容从右边滑进来淡入
    QRectF rt[3]; int tt[3]; layoutFor(m_step, rt, tt);
    QRectF rf[3]; int tf[3];
    if (m_prevStep >= 0) layoutFor(m_prevStep, rf, tf);

    for (int col = 0; col < 3; ++col) {
        const double t = m_panelT[col];

        if (t >= 1.0 || m_prevStep < 0) {
            drawColumn(p, col, m_step, rt[col]);
            continue;
        }

        const QRectF rc = rt[col];
        const double e  = easeOut(t);
        const double dx = 14.0;

        // 裁剪区取【新旧矩形的并集】——
        // 之前只裁到新矩形，新矩形比旧的小的时候旧内容会被瞬间切掉，那就是"抽动"
        p.save();
        QPainterPath cp;
        cp.addRoundedRect(rf[col].united(rc), 18, 18);
        p.setClipPath(cp);

        // 旧内容：往左滑出去 + 淡出
        p.save();
        p.setOpacity(1.0 - e);
        p.translate(-dx * e, 0.0);
        drawColumn(p, col, m_prevStep, rf[col]);
        p.restore();

        // 新内容：从右边滑进来 + 淡入
        p.save();
        p.setOpacity(e);
        p.translate(dx * (1.0 - e), 0.0);
        drawColumn(p, col, m_step, rc);
        p.restore();

        p.restore();
    }

    drawTicker(p);
}

// ==================================================================
//  顶栏：线路 + 状态文字（这一屏的核心提示）+ 时间日期
// ==================================================================
// ★ 宽度兜底：在指定矩形里画文字，超宽就自动缩字号（最小到 minPx）
// 全工程统一用它，换更长的站名/提示语也不会破版
void TrainScreen::fittedText(QPainter &p, const QString &s, const QRectF &r, int maxPx, int minPx,
                             const QColor &col, bool bold, int flags)
{
    int px = maxPx;
    while (px > minPx && QFontMetricsF(fnt(px, bold)).horizontalAdvance(s) > r.width())
        --px;
    p.setFont(fnt(px, bold));
    p.setPen(col);
    p.drawText(r, flags, s);
}

// 右上角空白区：拥挤度小条 / 车厢号 / 空调（从右往左排，蓝色标签）
// 键位：Y 自动随机（到站变） · + / - 加减 · Tab 切车厢 · C 强冷↔弱冷 · H 制热 · O 关闭
void TrainScreen::drawCabinInfo(QPainter &p)
{
    const double cy = R_HEADER.center().y();
    double x = R_HEADER.right() - 196.0;      // 右边让给时间/日期

    auto blueLabel = [&](const QString &s, double gapAfter) {
        const QFont f = fnt(12, true);
        p.setFont(f);
        const QFontMetricsF fm(f);
        p.setPen(kBlue);
        p.drawText(QPointF(x - fm.horizontalAdvance(s),
                           cy + (fm.ascent() - fm.descent()) / 2.0), s);
        x -= fm.horizontalAdvance(s) + gapAfter;
    };

    // ① 拥挤度（最右）：5 格小条，按档位变色
    {
        const double bw = 16, bh = 12, g = 3;
        const double totalW = 5 * bw + 4 * g;
        QColor c = kTextWeak;
        if (m_crowd <= 2)      c = QColor(60, 185, 90);
        else if (m_crowd == 3) c = QColor(232, 172, 40);
        else                   c = kLine;
        for (int i = 0; i < 5; ++i) {
            const QRectF seg(x - totalW + i * (bw + g), cy - bh / 2.0, bw, bh);
            p.setPen(Qt::NoPen);
            p.setBrush(i < m_crowd ? c : QColor(0, 0, 0, 26));
            p.drawRoundedRect(seg, 2, 2);
        }
        x -= totalW + 8;
        if (m_crowdAuto) {   // 自动模式下加个小点提示
            p.setPen(Qt::NoPen);
            p.setBrush(kBlue);
            p.drawEllipse(QPointF(x - 8, cy), 3.5, 3.5);
            x -= 18;
        }
        blueLabel(QStringLiteral("拥挤度"), 22);
    }

    // ② 车厢号
    {
        const QFont vf = fnt(14, true);
        p.setFont(vf);
        const QFontMetricsF vfm(vf);
        const QString val = QStringLiteral("%1号车厢").arg(m_carNo);
        p.setPen(kText);
        p.drawText(QPointF(x - vfm.horizontalAdvance(val),
                           cy + (vfm.ascent() - vfm.descent()) / 2.0), val);
        x -= vfm.horizontalAdvance(val) + 6;
        blueLabel(QStringLiteral("车厢"), 24);
    }

    // ③ 空调
    {
        static const char *modes[4] = { "强冷", "弱冷", "制热", "关闭" };
        const QFont vf = fnt(14, true);
        p.setFont(vf);
        const QFontMetricsF vfm(vf);
        const QString val = QString::fromUtf8(modes[qBound(0, m_acMode, 3)]);
        p.setPen(m_acMode == 2 ? kWarm : (m_acMode == 3 ? kTextWeak : kBlue));
        p.drawText(QPointF(x - vfm.horizontalAdvance(val),
                           cy + (vfm.ascent() - vfm.descent()) / 2.0), val);
        x -= vfm.horizontalAdvance(val) + 6;
        blueLabel(QStringLiteral("空调"), 0);
    }
}

void TrainScreen::drawHeader(QPainter &p)
{
    // 线路徽标
    const QRectF b(R_HEADER.left() + 12, R_HEADER.top() + 8, 38, 38);
    p.setPen(Qt::NoPen);
    p.setBrush(kLine);
    p.drawRoundedRect(b, 11, 11);
    fittedText(p, m_badge, b, 23, 14, Qt::white, true, Qt::AlignCenter);

    // 线路名 / 英文名：太长自动缩字号，别压到右边的"开往"标记
    const double nameAvail = (R_HEADER.left() + 186) - (b.right() + 11) - 8.0;
    fittedText(p, m_lineName, QRectF(b.right() + 11, R_HEADER.top() + 4, nameAvail, 30),
               21, 12, kText, true, Qt::AlignLeft);
    fittedText(p, m_lineNameEn, QRectF(b.right() + 12, R_HEADER.top() + 34, nameAvail, 22),
               10, 8, kTextSub, false, Qt::AlignLeft);

    // ---- 行驶方向标记（全局，所有页面都有，做显眼一点）----
    {
        const QRectF dirPill(R_HEADER.left() + 186, R_HEADER.center().y() - 18, 310, 36);
        p.setPen(Qt::NoPen);
        p.setBrush(kLine);
        p.drawRoundedRect(dirPill, 11, 11);

        // 三个小箭头：亮点顺着【行驶方向】流过去（相位前移→亮斑往车的朝向走）
        for (int i = 0; i < 3; ++i) {
            const int k = (m_dir > 0) ? i : (2 - i);
            double ph = std::fmod(m_globalT * 1.5 - k * 0.34, 1.0);
            if (ph < 0.0) ph += 1.0;      // fmod 会返回负值，补回来免得箭头直接不画
            const double a = 0.30 + 0.70 * (1.0 - std::fabs(ph * 2.0 - 1.0));
            chevrons(p, QPointF(dirPill.left() + 23 + i * 20.0, dirPill.center().y()),
                     m_dir < 0, a, 8.0, Qt::white);
        }
        p.setFont(fnt(21, true));
        p.setPen(Qt::white);
        const QFontMetricsF hfm(p.font());
        p.drawText(QPointF(dirPill.left() + 88,
                           dirPill.center().y() + (hfm.ascent() - hfm.descent()) / 2.0),
                   QStringLiteral("开往 %1").arg(directionTerminal().name));
    }

    // ---- 右侧：时间 + 日期（横排，绝不重叠）----
    const QDateTime now = QDateTime::currentDateTime();
    const QString hm = now.toString(QStringLiteral("HH:mm"));
    const QStringList wd = { QStringLiteral("周日"), QStringLiteral("周一"), QStringLiteral("周二"),
                             QStringLiteral("周三"), QStringLiteral("周四"), QStringLiteral("周五"),
                             QStringLiteral("周六") };
    const QDate d = now.date();
    const QString ds = QStringLiteral("%1.%2 %3")
            .arg(d.month(), 2, 10, QChar('0'))
            .arg(d.day(),   2, 10, QChar('0'))
            .arg(wd.at(d.dayOfWeek() % 7));

    const double rightEdge = R_HEADER.right() - 14;
    const QFont tf = fnt(27, true);
    p.setFont(tf);
    QFontMetricsF tfm(tf);
    const double timeW = tfm.horizontalAdvance(hm);
    p.setPen(kText);
    p.drawText(QPointF(rightEdge - timeW, R_HEADER.center().y() + 1 + (tfm.ascent() - tfm.descent()) / 2.0), hm);

    const QFont sf = fnt(13);
    p.setFont(sf);
    QFontMetricsF sfm(sf);
    p.setPen(kTextSub);
    const double dateX = rightEdge - timeW - 16 - sfm.horizontalAdvance(ds);
    p.drawText(QPointF(dateX, R_HEADER.center().y() + 2 + (sfm.ascent() - sfm.descent()) / 2.0), ds);

    // ---- 中间：状态文字（本屏核心提示，随翻页缓入）----
    const QString st = statusText();
    {
        // 状态文字也是直接刷新，不做动画
        const QFont stf = fnt(24, true);
        p.setFont(stf);
        const QFontMetricsF stfm(stf);
        const double stW = stfm.horizontalAdvance(st);
        const double stX = R_HEADER.left() + 520;   // 让开左边的"开往 XX"标记
        const double avail = dateX - 20 - stX;
        if (stW < avail) {
            const QColor col = (m_step == StepArriving || m_step == StepStopped) ? kLine : kText;
            p.setPen(Qt::NoPen);
            const bool hot = (m_step == StepArriving || m_step == StepStopped);
            const double pulse = hot ? (0.55 + 0.45 * (0.5 + 0.5 * std::sin(m_globalT * 5.0))) : 1.0;
            p.setBrush(withAlpha(col, int(255 * pulse)));
            p.drawEllipse(QPointF(stX - 16, R_HEADER.center().y()), 5, 5);
            p.setPen(col);
            p.drawText(QPointF(stX, R_HEADER.center().y() + 1 + (stfm.ascent() - stfm.descent()) / 2.0), st);
        }
    }
    // 右上角：拥挤度 / 车厢号 / 空调
    drawCabinInfo(p);

    // 底部细线
    p.setPen(Qt::NoPen);
    p.setBrush(kDivider);
    p.drawRect(QRectF(18, R_HEADER.bottom() + 7, 1564, 1.5));
}

// （三栏的内容由 paint() 按 layoutFor() 逐栏绘制，这里不再需要整页函数）

// ==================================================================
//  左栏：信息卡  mode 0=下一站  1=即将到站  2=本站
// ==================================================================
void TrainScreen::drawInfoCard(QPainter &p, const QRectF &r, int mode)
{
    const Station &n = showStation();

    panel(p, r, 18, kCard);

    const QString tag = infoTag(mode, atDirectionTerminus());

    // 标签（线路色胶囊）
    pill(p, r.left() + 18, r.top() + 14, 30, tag, kLineSoft, kLine, false);

    // 站名 / 英文名：超宽自动缩字号
    fittedText(p, n.name, QRectF(r.left() + 18, r.top() + 54, r.width() - 36, 46),
               40, 20, kText, true, Qt::AlignLeft | Qt::AlignVCenter);
    fittedText(p, n.en, QRectF(r.left() + 20, r.top() + 102, r.width() - 40, 22),
               14, 9, kTextSub, false, Qt::AlignLeft | Qt::AlignVCenter);

    // 分隔线
    p.setPen(Qt::NoPen);
    p.setBrush(kDivider);
    p.drawRect(QRectF(r.left() + 18, r.top() + 132, r.width() - 36, 1.5));

    // 换乘（用对方线路自己的颜色）
    label(p, QStringLiteral("换乘 TRANSFER"), r.left() + 18, r.top() + 150, 11, kTextSub);
    double bx = r.left() + 18;
    for (int i = 0; i < n.transfers.size(); ++i) {
        const QString t = n.transfers.at(i);
        bx = pill(p, bx, r.top() + 160, 30, t, lineColorOf(t), Qt::white, false);
    }
    if (n.transfers.isEmpty())
        label(p, QStringLiteral("—"), bx, r.top() + 175, 13, kTextWeak);

    // 出入口
    label(p, QStringLiteral("出入口 EXIT"), r.left() + 18, r.top() + 196, 11, kTextSub);
    double ex = r.left() + 18;
    for (int i = 0; i < n.exits.size(); ++i)
        ex = pill(p, ex, r.top() + 206, 30, QStringLiteral("出口 ") + n.exits.at(i),
                  kCard2, kText, true);
}

// ==================================================================
//  开门方向：一扇真正的地铁门（纯白门叶 + 长方形窗）
// ==================================================================
void TrainScreen::drawDoorCard(QPainter &p, const QRectF &r, int mode)
{
    const bool openLeft = (showStation().doorSide < 0);
    const bool closing  = (mode == DoorClosing);
    const bool term     = atDirectionTerminus();

    panel(p, r, 18, closing ? QColor(253, 244, 246) : kCard);

    label(p, closing ? QStringLiteral("车门状态 · DOORS") : QStringLiteral("开门方向 · DOORS"),
          r.left() + 18, r.top() + 20, 12, closing ? kLine : kTextSub);

    // 大字：关门时直接喊"车门即将关闭"；开门/已开时写哪侧
    const int bigPx = qBound(22, int(r.width() * 0.066), 29);
    p.setPen(kLine);
    p.setFont(fnt(bigPx, true));
    p.drawText(QRectF(r.left() + 14, r.top() + 32, r.width() - 28, bigPx + 14),
               Qt::AlignHCenter | Qt::AlignVCenter,
               closing ? QStringLiteral("车门即将关闭")
                       : (openLeft ? QStringLiteral("左侧开门") : QStringLiteral("右侧开门")));

    // ---------- 门的图形 ----------
    const double doorW = qMin(r.width() * 0.42, 96.0);
    const double doorH = qMin(r.bottom() - r.top() - 118.0, doorW * 1.42);
    const QRectF door(r.center().x() - doorW / 2.0, r.top() + 74, doorW, doorH);

    // 开门：0→全开循环；关门：全开→全关（镜像）；静态：一直全开
    double o = 1.0;
    if (mode == DoorOpening)      o = doorOpen(m_globalT);
    else if (mode == DoorClosing) o = doorClose(m_globalT);

    // 门套
    p.setPen(QPen(QColor(178, 188, 200), 2.0));
    p.setBrush(QColor(237, 240, 244));
    p.drawRoundedRect(door, 8, 8);

    // 门内（滑开后露出来的站台，浅色，不发绿光）
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(door.adjusted(4, 4, -4, -4), 6, 6);
    p.setClipPath(clip);
    p.fillRect(door, QColor(250, 251, 252));

    const double inner  = door.width() - 8.0;
    const double leafW  = inner / 2.0;
    const double slide  = o * leafW;

    // 一扇门叶：纯白 + 上面一个长方形窗
    auto leaf = [&](double x) {
        const QRectF lf(x, door.top() + 4, leafW * 0.96, door.height() - 8);
        p.setPen(QPen(QColor(160, 172, 186), 1.8));
        p.setBrush(Qt::white);
        p.drawRoundedRect(lf, 3, 3);
        const QRectF win(lf.left() + lf.width() * 0.18, lf.top() + lf.height() * 0.16,
                         lf.width() * 0.64, lf.height() * 0.30);
        p.setPen(QPen(QColor(142, 166, 194), 1.6));
        p.setBrush(QColor(190, 212, 234));
        p.drawRoundedRect(win, 2, 2);
    };
    leaf(door.left() + 4 - slide);
    leaf(door.left() + 4 + leafW + slide);
    p.restore();

    // 门套再描一遍（盖住滑出去的门叶）
    p.setPen(QPen(QColor(150, 162, 176), 2.2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(door, 8, 8);

    // 地面线
    p.setPen(QPen(QColor(186, 194, 204), 2.5));
    p.drawLine(QPointF(door.left() - 8, door.bottom() + 4), QPointF(door.right() + 8, door.bottom() + 4));

    // 底部提示
    QString hint;
    if (closing)                          hint = QStringLiteral("请注意安全 · 请勿抢上抢下");
    else if (term && mode == DoorOpenStatic) hint = QStringLiteral("终点站 · 请所有乘客下车");
    else                                  hint = QStringLiteral("请在%1下车").arg(openLeft ? QStringLiteral("左侧") : QStringLiteral("右侧"));
    // 底部提示：居中；太宽就自动缩字号（不会压出卡片）
    {
        int px = 13;
        while (px > 9 &&
               QFontMetricsF(fnt(px, closing)).horizontalAdvance(hint) > r.width() - 14.0)
            --px;
        p.setFont(fnt(px, closing));
        p.setPen(closing ? kLine : kTextSub);
        p.drawText(QRectF(r.left() + 7.0, r.bottom() - 32.0, r.width() - 14.0, 26.0),
                   Qt::AlignHCenter | Qt::AlignVCenter, hint);
    }

    // 关门警示：整卡流光扫过（关门是最容易出事的环节，得够显眼）
    if (closing) {
        p.save();
        QPainterPath c2;
        c2.addRoundedRect(r.adjusted(0.6, 0.6, -0.6, -0.6), 18, 18);
        p.setClipPath(c2);
        const double band = r.width() * 0.30;
        const double ph = std::fmod(m_globalT * 0.55, 1.6) / 1.6;
        const double cx = r.left() - band + (r.width() + band * 2.0) * ph;
        QLinearGradient lg(cx - band, 0, cx + band, 0);
        lg.setColorAt(0.0, QColor(200, 16, 46, 0));
        lg.setColorAt(0.5, QColor(200, 16, 46, 52));
        lg.setColorAt(1.0, QColor(200, 16, 46, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(lg);
        p.drawRect(r);
        p.restore();
    }
}

// ==================================================================
//  闪灯图（亮色版）
// ==================================================================
// 小铁路标志（简化版中国铁路徽）：红底圆 + 白色钢轨形
void TrainScreen::drawRailMark(QPainter &p, const QPointF &c, double r)
{
    p.setPen(Qt::NoPen);
    p.setBrush(kLine);
    p.drawEllipse(c, r, r);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, qMax(1.2, r * 0.17), Qt::SolidLine, Qt::RoundCap));
    QPainterPath path;                       // 两条上扬的钢轨
    path.moveTo(c.x() - r * 0.46, c.y() - r * 0.08);
    path.lineTo(c.x(), c.y() - r * 0.50);
    path.lineTo(c.x() + r * 0.46, c.y() - r * 0.08);
    p.drawPath(path);
    p.drawLine(QPointF(c.x(), c.y() - r * 0.50), QPointF(c.x(), c.y() + r * 0.52));   // 轨身
    p.drawLine(QPointF(c.x() - r * 0.40, c.y() + r * 0.14),
               QPointF(c.x() + r * 0.40, c.y() + r * 0.14));                          // 中间横杠
}

void TrainScreen::drawRouteMap(QPainter &p, const QRectF &r)
{
    panel(p, r, 18, kCard);

    // 标题（"开往 XX 方向"删了 —— 顶栏左侧已经有明显的方向标记，这里会跟终点站标签打架）
    fittedText(p, QStringLiteral("闪灯图 · ROUTE MAP"),
               QRectF(r.left() + 16, r.top() + 6, r.width() - 32, 26),
               12, 9, kTextSub, false, Qt::AlignLeft | Qt::AlignVCenter);

    const double y  = r.top() + 122.0;
    const double x0 = r.left() + 60.0;
    const double x1 = r.right() - 60.0;
    const int ni = nextIndex();

    // 闪灯图显示范围：只有"即将到站"这一屏会收窄（最多 6 个站），
    // 其余屏（即将关门 / 行驶中）显示【完整线路图】
    int wf = 0, wt = m_stations.size() - 1;
    mapWindow(wf, wt);
    const int span = qMax(1, wt - wf);
    auto nodeX = [&](int i) { return lerp(x0, x1, double(qBound(wf, i, wt) - wf) / double(span)); };

    // 站名 / 英文名：站密了就自动缩字号，别挤在一起
    const double spacing = (x1 - x0) / double(span);
    const int namePx = qBound(13, int(spacing * 2.0 / 4.4), 21);
    const int enPx   = qBound(9,  int(spacing * 2.0 / 9.0), 11);

    // 轨道本体（只画窗口这一段）
    p.setPen(QPen(kRail, 10, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(nodeX(wf), y), QPointF(nodeX(wt), y));

    // 两头若还有站：拉出一条【渐隐】的横线，表示"后边/前边还有站"
    p.save();
    p.setClipRect(r);
    if (wt < m_stations.size() - 1) {
        const double xa = nodeX(wt);
        QLinearGradient lg(xa, 0, r.right(), 0);
        lg.setColorAt(0.0, kRail);
        lg.setColorAt(0.55, withAlpha(kRail, 150));
        lg.setColorAt(1.0, withAlpha(kRail, 0));
        p.setPen(QPen(QBrush(lg), 10, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(xa, y), QPointF(r.right(), y));
    }
    if (wf > 0) {
        const double xa = nodeX(wf);
        QLinearGradient lg(r.left(), 0, xa, 0);
        lg.setColorAt(0.0, withAlpha(kRail, 0));
        lg.setColorAt(0.45, withAlpha(kRail, 150));
        lg.setColorAt(1.0, kRail);
        p.setPen(QPen(QBrush(lg), 10, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(r.left(), y), QPointF(xa, y));
    }
    p.restore();
    {
        const double cx = nodeX(m_curIndex);
        const double tx = nodeX(ni);
        const double px = lerp(cx, tx, m_carPos);
        if (std::fabs(px - cx) > 1.5) {
            p.setPen(QPen(kRailOld, 10, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(cx, y), QPointF(px, y));
        }
    }

    // 车：纯色小圆角块 + 一道白窗线
    const double tx = lerp(nodeX(m_curIndex), nodeX(ni), m_carPos);
    {
        const QRectF tr(tx - 22, y - 13, 44, 26);
        p.setPen(Qt::NoPen);
        p.setBrush(kLine);
        p.drawRoundedRect(tr, 6, 6);
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(tr.left() + 7, tr.center().y() - 2.0, tr.width() - 14, 4), 2, 2);
    }

    // 车站节点（只画窗口内的）
    for (int i = wf; i <= wt; ++i) {
        const QPointF c(nodeX(i), y);
        const bool isNext = (i == ni);
        const bool isCur  = (i == m_curIndex);
        const bool isTerm = isTerminal(i);

        if (isNext) {
            const double ph = std::fmod(m_globalT, 1.4) / 1.4;
            for (int k = 0; k < 3; ++k) {
                const double kk = std::fmod(ph + k / 3.0, 1.0);
                p.setPen(QPen(withAlpha(kLine, int(90 * (1.0 - kk))), 2.5));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(c, 12 + 22 * kk, 12 + 22 * kk);
            }
        }

        // 圆圈：换乘站用换乘线路的颜色（2号线蓝 / 3号线黄）；当前/下一站用线路红
        QColor ringCol = isNext ? kLine : kTextWeak;
        QColor dotCol  = Qt::white;
        if (!isNext && !isCur && !m_stations.at(i).transfers.isEmpty()) {
            ringCol = lineColorOf(m_stations.at(i).transfers.first());
            dotCol  = ringCol;
        }
        p.setPen(QPen(ringCol, 2.5));
        p.setBrush(isNext ? kLine : (isCur ? kLine : Qt::white));
        p.drawEllipse(c, 12, 12);
        p.setPen(Qt::NoPen);
        p.setBrush((isNext || isCur) ? Qt::white : dotCol);
        p.drawEllipse(c, 5, 5);

        // 站名 / 英文名：单双号交替上下排（每个名字的横向空间翻倍），不再画"本站/下一站"
        const bool hot = (isCur || isNext);
        const bool up  = (i % 2 != 0);            // 第 2、4、6……个站的名字排在轨道上方
        const double nameW = qMin(spacing * 2.0 - 10.0, r.width() - 12.0);
        double nx = c.x() - nameW / 2.0;
        nx = qBound(r.left() + 6.0, nx, r.right() - 6.0 - nameW);
        const double nameY = up ? (y - 52.0) : (y + 24.0);
        const double enY   = up ? (y - 78.0) : (y + 52.0);
        fittedText(p, m_stations.at(i).name,
                   QRectF(nx, nameY, nameW, 28),
                   namePx, 10, hot ? kText : kTextSub, true, Qt::AlignHCenter | Qt::AlignTop);
        fittedText(p, m_stations.at(i).en,
                   QRectF(nx, enY, nameW, 20),
                   enPx, 8, kTextWeak, false, Qt::AlignHCenter | Qt::AlignTop);

        if (isTerm) {
            p.setFont(fnt(10, true));
            p.setPen(kTextSub);
            p.drawText(QRectF(c.x() - 60, up ? (y - 100.0) : (y + 74.0), 120, 18),
                       Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("终点站"));
        }
    }
}

// ==================================================================
//  站台布局（亮色版）  opened = 门是否在开
// ==================================================================
void TrainScreen::drawStationLayout(QPainter &p, const QRectF &r, bool opened)
{
    const Station &st = showStation();
    const bool openLeft = (st.doorSide < 0);

    panel(p, r, 18, kCard);

    // ---- 顶部横幅 ----
    const QRectF ban(r.left() + 14, r.top() + 12, r.width() - 28, 36);
    p.setPen(Qt::NoPen);
    p.setBrush(opened ? kLineSoft : QColor(242, 244, 247));
    p.drawRoundedRect(ban, 12, 12);

    // 横幅文案把"哪侧开门"写清楚（方向由顶栏标记统一表达，这里不再放箭头）
    const QString sideTxt = openLeft ? QStringLiteral("左侧") : QStringLiteral("右侧");
    const QString banTxt = opened
            ? (atDirectionTerminus() ? QStringLiteral("终点站 %1 · %2车门开启").arg(st.name, sideTxt)
                                       : QStringLiteral("本站 %1 · %2车门开启").arg(st.name, sideTxt))
            : (atDirectionTerminus() ? QStringLiteral("即将到达终点站 %1 · %2开门").arg(st.name, sideTxt)
                                       : QStringLiteral("即将到达 %1 · %2开门").arg(st.name, sideTxt));
    fittedText(p, banTxt, QRectF(ban.left() + 14, ban.top(), ban.width() - 28, ban.height()),
               16, 11, opened ? kLine : kTextSub, true, Qt::AlignLeft | Qt::AlignVCenter);

    // ---- 布局区 ----
    const QRectF lay(r.left() + 14, r.top() + 56, r.width() - 28, r.height() - 56 - 12);

    const double tw = 80.0;
    const double trackGap = 88.0;
    double plW = (lay.width() - tw - 2 * trackGap - 2 * 14.0) / 2.0;
    plW = qBound(150.0, plW, 340.0);
    const double total = plW * 2 + tw + trackGap * 2;
    const double left0 = lay.center().x() - total / 2.0;

    const QRectF plL(left0, lay.top(), plW, lay.height());
    const QRectF plR(left0 + total - plW, lay.top(), plW, lay.height());
    const QRectF train(left0 + plW + trackGap, lay.top() + 4, tw, lay.height() - 8);

    // 轨道床
    p.setPen(Qt::NoPen);
    p.setBrush(kBed);
    p.drawRect(QRectF(plL.right(), lay.top(), plR.left() - plL.right(), lay.height()));

    // 站台
    auto platform = [&](const QRectF &pl, bool isOpenSide) {
        p.setPen(QPen(kEdge, 1.2));
        p.setBrush(isOpenSide ? kLineSoft : kCard2);
        p.drawRoundedRect(pl, 12, 12);
    };
    platform(plL, openLeft);
    platform(plR, !openLeft);

    // 屏蔽门（站台靠车那一侧的边线）
    auto psb = [&](double x, bool isOpenSide) {
        p.setPen(QPen(isOpenSide ? kLine : kTextWeak, 2.5));
        p.drawLine(QPointF(x, lay.top() + 8), QPointF(x, lay.bottom() - 8));
    };
    psb(plL.right(), openLeft);
    psb(plR.left(), !openLeft);

    // 车厢与开门侧站台之间的空档：竖排红色大字 —— 别让人猜哪边开门
    {
        const double bandX = openLeft ? (plL.right() + train.left()) / 2.0
                                      : (train.right() + plR.left()) / 2.0;
        const double bh = 4 * 25.0 + 14.0;
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(kLine, 28));
        p.drawRoundedRect(QRectF(bandX - 19, lay.center().y() - bh / 2.0, 38, bh), 12, 12);
        verticalText(p, openLeft ? QStringLiteral("左侧开门") : QStringLiteral("右侧开门"),
                     bandX, lay.center().y(), 21, kLine);
    }

    // ---- 车厢 ----
    {
        p.setPen(QPen(QColor(214, 219, 226), 1.4));
        p.setBrush(Qt::white);
        p.drawRoundedRect(train, 18, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(232, 236, 241));
        const double wy = train.top() + 16;
        for (int i = 0; i < 3; ++i)
            p.drawRoundedRect(QRectF(train.center().x() - 14, wy + i * 38, 28, 16), 4, 4);
    }

    // ---- 车门 ----
    const int doorCount = 3;
    const double doorH = 28.0;
    const double firstY = train.top() + 10.0;
    const double gapY = (train.height() - 20.0 - doorH) / double(doorCount - 1);
    const double o = opened ? doorOpen(m_globalT) : 0.0;

    for (int i = 0; i < doorCount; ++i) {
        const double dy = firstY + i * gapY;
        const double dh = doorH * (opened ? (1.0 + 0.25 * o) : 1.0);
        // 不开门侧：浅灰门槽
        p.setPen(Qt::NoPen);
        p.setBrush(openLeft ? QColor(232, 236, 241) : QColor(206, 212, 220));
        p.drawRoundedRect(QRectF(train.left() - 1, dy, 14, doorH), 3, 3);
        p.setBrush(!openLeft ? QColor(232, 236, 241) : QColor(206, 212, 220));
        p.drawRoundedRect(QRectF(train.right() - 13, dy, 14, doorH), 3, 3);
        (void)dh;
    }

    // 开门侧：门开出来的白色门叶 + 浅色开口
    if (opened) {
        const double dir = openLeft ? -1.0 : 1.0;
        const double edgeX = openLeft ? train.left() : train.right();
        for (int i = 0; i < doorCount; ++i) {
            const double dy = firstY + i * gapY;
            // 门叶滑出去一点点
            p.setPen(QPen(QColor(214, 219, 226), 1.1));
            p.setBrush(Qt::white);
            p.drawRoundedRect(QRectF(edgeX + dir * (2 + 8 * o) - 6, dy + 1, 12, doorH - 2), 3, 3);
        }
    }

    // ---- 乘客流线（只在停稳开门时）----
    if (opened) {
        const double dir = openLeft ? -1.0 : 1.0;
        const double edge = openLeft ? train.left() : train.right();
        const double platEdge = openLeft ? plL.right() - 24 : plR.left() + 24;
        for (int i = 0; i < 9; ++i) {
            const int di = i % doorCount;
            const double dy = firstY + di * gapY + doorH / 2.0;
            const double jitter = (pseudo(i) - 0.5) * 44.0;
            const QPointF a(edge, dy);
            const QPointF b(platEdge + dir * (8 + pseudo(i + 40) * 60), dy + jitter);
            const double t = clamp01(std::fmod(m_globalT * 0.5 + i * 0.11, 1.0));
            const int alpha = int(220 * (1.0 - clamp01((t - 0.72) / 0.28)));
            if (alpha <= 6) continue;
            p.setPen(Qt::NoPen);
            p.setBrush(withAlpha(QColor(232, 163, 61), alpha));
            const QPointF pos(lerp(a.x(), b.x(), easeOut(t)), lerp(a.y(), b.y(), easeOut(t)));
            p.drawEllipse(pos, 4.2, 4.2);
        }
    }

    // ---- 站台设施（两侧对称，各一整套）----
    auto drawFacility = [&](const QRectF &box, const QString &type, const QString &text,
                            const QColor &col, bool dim, bool mirrorArrow = false) {
        const QColor c = dim ? withAlpha(col, 110) : col;
        const double l = box.left(), t = box.top(), bb = box.bottom(), rr = box.right();
        p.save();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

        if (type == QStringLiteral("stair")) {
            QPainterPath path;
            path.moveTo(l + 2, bb - 3);
            path.lineTo(l + 8, bb - 3);   path.lineTo(l + 8, bb - 10);
            path.lineTo(l + 15, bb - 10); path.lineTo(l + 15, bb - 17);
            path.lineTo(l + 22, bb - 17); path.lineTo(l + 22, bb - 24);
            path.lineTo(rr - 2, bb - 24);
            p.drawPath(path);
        } else if (type == QStringLiteral("escalator")) {
            p.drawLine(QPointF(l + 3, bb - 3), QPointF(rr - 3, t + 3));
            for (int k = 0; k < 3; ++k) {
                const double f = 0.18 + k * 0.28;
                const QPointF c1(l + 3 + (rr - l - 6) * f, bb - 3 - (bb - t - 6) * f);
                p.drawLine(c1, c1 + QPointF(4.5, 4.5));
            }
        } else if (type == QStringLiteral("lift")) {
            p.drawRoundedRect(box.adjusted(2, 2, -2, -2), 4, 4);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            QPainterPath up;
            up.moveTo(box.center().x(), t + 6);
            up.lineTo(box.center().x() - 5, t + 13);
            up.lineTo(box.center().x() + 5, t + 13);
            up.closeSubpath();
            p.drawPath(up);
            QPainterPath dn;
            dn.moveTo(box.center().x(), bb - 6);
            dn.lineTo(box.center().x() - 5, bb - 13);
            dn.lineTo(box.center().x() + 5, bb - 13);
            dn.closeSubpath();
            p.drawPath(dn);
        } else if (type == QStringLiteral("wc")) {
            p.drawRoundedRect(box.adjusted(2, 2, -2, -2), 4, 4);
            p.setFont(fnt(12, true));
            p.setPen(c);
            p.drawText(box, Qt::AlignCenter, QStringLiteral("WC"));
        } else if (type == QStringLiteral("exit")) {
            p.drawRoundedRect(box.adjusted(2, 2, -2, -2), 4, 4);
            if (!mirrorArrow) {
                p.drawLine(QPointF(l + 6, box.center().y()), QPointF(rr - 10, box.center().y()));
                QPainterPath ar;
                ar.moveTo(rr - 8, box.center().y() - 5);
                ar.lineTo(rr - 3, box.center().y());
                ar.lineTo(rr - 8, box.center().y() + 5);
                p.drawPath(ar);
            } else {
                p.drawLine(QPointF(rr - 6, box.center().y()), QPointF(l + 10, box.center().y()));
                QPainterPath ar;
                ar.moveTo(l + 8, box.center().y() - 5);
                ar.lineTo(l + 3, box.center().y());
                ar.lineTo(l + 8, box.center().y() + 5);
                p.drawPath(ar);
            }
        }
        p.restore();

        p.setFont(fnt(10));
        p.setPen(dim ? kTextWeak : kTextSub);
        p.drawText(QRectF(box.left() - 26, box.bottom() + 1, box.width() + 52, 15),
                   Qt::AlignHCenter | Qt::AlignTop, text);
    };

    auto fillPlatform = [&](const QRectF &pl, const QString &exitLabel, bool dim) {
        const QString types[5] = { QStringLiteral("stair"), QStringLiteral("escalator"),
                                   QStringLiteral("lift"), QStringLiteral("wc"), QStringLiteral("exit") };
        const QString names[4] = { QStringLiteral("楼梯"), QStringLiteral("扶梯"),
                                   QStringLiteral("直梯"), QStringLiteral("卫生间") };
        for (int i = 0; i < 5; ++i) {
            double x;
            if (i < 3) x = pl.left() + pl.width() * (2 * i + 1) / 6.0;
            else       x = pl.left() + pl.width() * (i - 2) / 3.0;
            const double yy = (i < 3) ? pl.top() + pl.height() * 0.30 : pl.top() + pl.height() * 0.70;
            const QRectF box(x - 13, yy - 20, 26, 26);
            const QColor col = (i == 4) ? kLine : QColor(78, 88, 101);
            const bool mirror = (pl.center().x() < lay.center().x());
            drawFacility(box, types[i], (i == 4) ? exitLabel : names[i], col, dim, mirror);
        }
    };

    fillPlatform(plL, QStringLiteral("出口 ") + st.exits.value(0, QStringLiteral("A")), !openLeft);
    fillPlatform(plR, QStringLiteral("出口 ") + st.exits.value(1, QStringLiteral("B")), openLeft);

    // 不开门侧：浅灰蒙一层 + 标识
    {
        const QRectF closedPl = openLeft ? plR : plL;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 165));
        p.drawRoundedRect(closedPl, 12, 12);
        p.setPen(QPen(kTextWeak, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(closedPl, 12, 12);
        p.setFont(fnt(12, true));
        p.setPen(kTextSub);
        p.drawText(closedPl, Qt::AlignCenter, QStringLiteral("此侧不开门"));
    }
}

// ==================================================================
//  底部滚动公告（裁切区从"公告"方块右侧开始）
// ==================================================================
void TrainScreen::drawTicker(QPainter &p)
{
    const QRectF bar = R_TICKER;
    p.setPen(QPen(kEdge, 1.2));
    p.setBrush(QColor(247, 248, 250));
    p.drawRoundedRect(bar, 12, 12);

    const QRectF tag(bar.left() + 6, bar.top() + 6, 68, bar.height() - 12);
    p.setPen(Qt::NoPen);
    p.setBrush(kLine);
    p.drawRoundedRect(tag, 9, 9);
    p.setFont(fnt(15, true));
    p.setPen(Qt::white);
    p.drawText(tag, Qt::AlignCenter, QStringLiteral("公告"));

    p.save();
    const QRectF clipRect(tag.right() + 10, bar.top() + 2,
                          bar.right() - tag.right() - 20, bar.height() - 4);
    p.setClipRect(clipRect);

    const QFont f = fnt(19);
    p.setFont(f);
    const QFontMetricsF fm(f);
    p.setPen(kText);
    for (int i = 0; i < m_tickerSegs.size(); ++i) {
        const QString &t = m_tickerSegs.at(i).txt;
        const double xx = m_tickerSegs.at(i).x;
        const double w = fm.horizontalAdvance(t);
        if (xx > clipRect.right() + 20) continue;
        if (xx + w < clipRect.left() - 20) continue;
        p.drawText(QPointF(xx, baselineY(bar, fm)), t);
    }
    p.restore();
}

// ==================================================================
//  小工具
// ==================================================================
void TrainScreen::panel(QPainter &p, const QRectF &r, double radius, const QColor &fill)
{
    p.setPen(QPen(kEdge, 1.2));
    p.setBrush(fill);
    p.drawRoundedRect(r.adjusted(0.6, 0.6, -0.6, -0.6), radius, radius);
}

void TrainScreen::chevrons(QPainter &p, const QPointF &c, bool pointLeft, double alpha,
                           double s, const QColor &col)
{
    if (alpha <= 0.01) return;
    p.save();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(withAlpha(col, int(255 * clamp01(alpha))), s * 0.5,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath path;
    if (pointLeft) {
        path.moveTo(c.x() + s, c.y() - s);
        path.lineTo(c.x() - s, c.y());
        path.lineTo(c.x() + s, c.y() + s);
    } else {
        path.moveTo(c.x() - s, c.y() - s);
        path.lineTo(c.x() + s, c.y());
        path.lineTo(c.x() - s, c.y() + s);
    }
    p.drawPath(path);
    p.restore();
}

void TrainScreen::label(QPainter &p, const QString &s, double x, double cy, int px,
                        const QColor &col, bool bold)
{
    p.setFont(fnt(px, bold));
    p.setPen(col);
    const QFontMetricsF fm(p.font());
    p.drawText(QPointF(x, cy + (fm.ascent() - fm.descent()) / 2.0), s);
}

double TrainScreen::pill(QPainter &p, double x, double y, double h, const QString &txt,
                         const QColor &fill, const QColor &txtCol, bool outline)
{
    const QFont f = fnt(int(h * 0.54), true);
    p.setFont(f);
    const QFontMetricsF fm(f);
    const double w = fm.horizontalAdvance(txt) + h * 1.15;
    const QRectF b(x, y, w, h);
    p.setPen(outline ? QPen(kEdge, 1.2) : QPen(Qt::NoPen));
    p.setBrush(fill);
    p.drawRoundedRect(b, h * 0.3, h * 0.3);
    p.setPen(txtCol);
    p.drawText(b, Qt::AlignCenter, txt);
    return x + w + 8.0;
}

// 竖排文字：每个字占一行（中文竖排的常规写法，不转向）
void TrainScreen::verticalText(QPainter &p, const QString &s, double cx, double cy,
                               int px, const QColor &col)
{
    p.setFont(fnt(px, true));
    p.setPen(col);
    const double step = px * 1.20;
    const double y0 = cy - (s.size() - 1) * step / 2.0;
    for (int i = 0; i < s.size(); ++i) {
        p.drawText(QRectF(cx - px, y0 + i * step - px * 0.9, px * 2.0, px * 1.8),
                   Qt::AlignCenter, QString(s.at(i)));
    }
}
