#ifndef SOUNDCUE_H
#define SOUNDCUE_H

#include <QString>
#include <QStringList>

// 提示音播放（Windows: winmm 的 PlaySound，零依赖）
//
//  ★ 全工程唯一入口，三条规则：
//    1) 打断式   —— 新音效一来，先硬停旧的，再播新的
//    2) 切页静音 —— TrainScreen::cueForStep() 开头会 stopAll()
//    3) 顺序播   —— playSeq() 会等上一段【放完】再播下一段（读 WAV 时长来排队）
//
//  音频放在 <程序目录>/assets/voice/ 下，只支持 WAV
class SoundCue
{
public:
    static bool enabled;                               // 静音开关（按 S 切换）
    static void play(const QString &fileName);         // 打断式，立刻播一段
    static void playSeq(const QStringList &fileNames); // 依次播，上一段放完才播下一段
    static double remainingSeconds();                  // 当前（或排队的最后一段）还剩多少秒
    static void stopAll();
    static QString voiceDir();

private:
    static void init();
};

#endif // SOUNDCUE_H
