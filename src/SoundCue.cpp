#include "SoundCue.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTimer>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <mmsystem.h>
#endif

bool SoundCue::enabled = true;

static QString     s_dir;
static int         s_gen = 0;      // 队列代号：新的一批会顶掉旧的回调
static QStringList s_queue;
static qint64      s_endMs = 0;    // 当前（或最后一段）预计播完的时刻

// ---- 读 WAV 头算时长（秒）：用来排"上一段放完再播下一段" ----
static double wavSeconds(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return 0.0;
    const QByteArray d = f.read(4096);
    if (d.size() < 44 || !d.startsWith("RIFF")) return 0.0;

    const uchar *p = reinterpret_cast<const uchar *>(d.constData());
    auto u32 = [p](int o) {
        return quint32(p[o]) | (quint32(p[o + 1]) << 8)
             | (quint32(p[o + 2]) << 16) | (quint32(p[o + 3]) << 24);
    };
    quint32 byteRate = 0, dataSize = 0;
    int pos = 12;
    while (pos + 8 <= d.size() && pos < 4000) {
        const QByteArray id = d.mid(pos, 4);
        const quint32 sz = u32(pos + 4);
        if (id == "fmt " && sz >= 12) {
            byteRate = u32(pos + 8 + 8);      // fmt 体里第 8 字节起是 byteRate
        } else if (id == "data") {
            dataSize = sz;
            break;
        }
        pos += 8 + int(sz) + (int(sz) & 1);
    }
    if (!byteRate || !dataSize) return 0.0;
    return double(dataSize) / double(byteRate);
}

static void playNextInQueue(int gen);     // 前向声明

QString SoundCue::voiceDir()
{
    init();
    return s_dir;
}

void SoundCue::init()
{
    if (!s_dir.isEmpty()) return;

    const QString base = QCoreApplication::applicationDirPath();
    const QStringList cand = {
        base + QLatin1String("/assets/voice"),                 // 部署后（CMake 会拷到 exe 旁边）
        base + QLatin1String("/../assets/voice"),              // build/
        base + QLatin1String("/../../assets/voice"),           // build-vs/Release/
        base + QLatin1String("/../../../assets/voice"),
        QDir::currentPath() + QLatin1String("/assets/voice"),
    };
    for (const QString &c : cand) {
        if (QFileInfo::exists(c + QLatin1String("/door_close.wav"))) {
            s_dir = QDir(c).absolutePath();
            return;
        }
    }
}

void SoundCue::play(const QString &fileName)
{
    if (!enabled) return;
    init();
    if (s_dir.isEmpty()) return;

    const QString path = QDir::toNativeSeparators(s_dir + QLatin1Char('/') + fileName);
    if (!QFileInfo::exists(path)) return;

#ifdef Q_OS_WIN
    // 打断式：先把正在播的那一段硬停掉，再起新的
    ::PlaySoundW(nullptr, nullptr, 0);
    ::PlaySoundW(reinterpret_cast<const wchar_t *>(path.utf16()),
                 nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    s_endMs = QDateTime::currentMSecsSinceEpoch() + qint64(wavSeconds(path) * 1000.0);
#endif
}

// 当前这段（或排队里最后一段）还剩多少秒；用于"自动播报"等它播完
double SoundCue::remainingSeconds()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    return s_endMs > now ? double(s_endMs - now) / 1000.0 : 0.0;
}

// 依次播：播完当前这段（按 WAV 时长等够）再播下一段
static void playNextInQueue(int gen)
{
    if (gen != s_gen) return;                 // 已经被新的一批顶掉了
    if (!SoundCue::enabled) { s_queue.clear(); return; }
    if (s_queue.isEmpty()) return;

    const QString f = s_queue.takeFirst();
    const double d = wavSeconds(SoundCue::voiceDir() + QLatin1Char('/') + f);
    SoundCue::play(f);

    if (!s_queue.isEmpty())
        QTimer::singleShot(int(d * 1000.0) + 90, [gen]() { playNextInQueue(gen); });
}

void SoundCue::playSeq(const QStringList &fileNames)
{
    if (!enabled || fileNames.isEmpty()) return;
    ++s_gen;                  // 顶掉上一批还没播完的
    s_queue = fileNames;
    playNextInQueue(s_gen);
}

void SoundCue::stopAll()
{
    ++s_gen;                  // 取消排队
    s_queue.clear();
    s_endMs = 0;
#ifdef Q_OS_WIN
    ::PlaySoundW(nullptr, nullptr, 0);
#endif
}
