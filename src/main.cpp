// metro-lcd —— 地铁车门上方横条 LCD 模拟（4:1）
// 龙虾线 · 3 站折返：黄台站 - 望江站 - 云溪站
// 全手动：← → 翻页，↑ ↓ 换向

#include <QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <QtGlobal>

#include "TrainScreen.h"

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Metro LCD"));

    TrainScreen w;

    // 调试/截图用：--step 0..3   --station 0..2   --dir 1|-1   --shot 输出.png
    int step = 0, station = 0, dir = 1;
    QString shotPath;
    double transT = -1.0;
    double prime  = 6.0;
    double shotTime = -1.0;   // --time：冻结全局动画时间（看流动箭头等）
    const QStringList a = QCoreApplication::arguments();
    for (int i = 1; i < a.size(); ++i) {
        const QString &s = a.at(i);
        if (s == QStringLiteral("--step") && i + 1 < a.size())           step = a.at(++i).toInt();
        else if (s == QStringLiteral("--station") && i + 1 < a.size())    station = a.at(++i).toInt();
        else if (s == QStringLiteral("--dir") && i + 1 < a.size())        dir = a.at(++i).toInt();
        else if (s == QStringLiteral("--shot") && i + 1 < a.size())       shotPath = a.at(++i);
        else if (s == QStringLiteral("--trans") && i + 1 < a.size())      transT = a.at(++i).toDouble();
        else if (s == QStringLiteral("--prime") && i + 1 < a.size())      prime = a.at(++i).toDouble();
        else if (s == QStringLiteral("--time") && i + 1 < a.size())       shotTime = a.at(++i).toDouble();
    }
    w.setState(step, station, dir);

    // 导出当前画面为 PNG 后直接退出（不弹窗）
    if (!shotPath.isEmpty()) {
        w.resize(1600, 400);
        w.setAnimTime(0.55);   // 固定一下动画相位，让开门/闪灯处于好看的中间状态
        w.primeAnim(prime);    // 先跑几秒模拟，让公告栏滚起来
        w.setAnimTime(0.55);
        if (shotTime >= 0.0) w.setAnimTime(shotTime);   // 冻结全局动画时间，看出图那一刻的流动状态
        if (transT >= 0.0) w.setTransition(transT);
        return w.grab().save(shotPath) ? 0 : 2;
    }

    w.setWindowTitle(QStringLiteral("地铁 LCD 模拟 · 迁安1号线 · 车门上方横条（4:1）"));
    w.resize(1280, 320);   // 正好 4:1
    w.show();

    return app.exec();
}
