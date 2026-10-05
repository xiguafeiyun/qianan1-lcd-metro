# -*- coding: utf-8 -*-
"""
按 line.json 自动生成全部报站语音（edge-tts -> mp3 -> 用 Chrome 转成 24kHz 单声道 WAV）

用法： python tools/gen_voice.py
产出： assets/voice/arr_zh_01.wav ... arr_en_16.wav / stop_*.wav / run_*.wav / close_*.wav
再次改线路后重跑一遍即可（已存在的会跳过）。
"""
import array, base64, io, json, os, re, subprocess, sys, tempfile, wave

ROOT      = r"D:\code\cpp\metro-lcd"
LINE_JSON = os.path.join(ROOT, "assets", "line.json")
VOICE_DIR = os.path.join(ROOT, "assets", "voice")
EDGE      = r"D:\Tools\aqtenv\Scripts\edge-tts.exe"
CHROME    = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
TMP       = r"D:\Temp\voicegen"

VOICE_ZH = "zh-CN-XiaoxiaoNeural"     # 中文女声（中文报站 + 英文播报里的中文站名）
VOICE_EN = "en-US-GuyNeural"          # 英文男声（只读英文句子）

LINE_ZH = "迁安1号线"
LINE_EN = "Qian'an Line 1"


def wav24_from_mp3_batch(items, out_dir):
    """items: [(mp3_path, out_wav_path)] —— 一次 Chrome 里批量解码，输出 24kHz 单声道 WAV"""
    payload = []
    for i, (mp3, _) in enumerate(items):
        payload.append('"%d": "%s"' % (i, base64.b64encode(open(mp3, "rb").read()).decode()))
    html = """<!DOCTYPE html><html><head><meta charset="utf-8"></head><body><pre id="out">x</pre>
<script>
var SRC = {%s};
var keys = Object.keys(SRC), res = {}, done = 0;
try {
  var ctx = new (window.AudioContext || window.webkitAudioContext)({sampleRate: 24000});
  keys.forEach(function (k) {
    var bin = atob(SRC[k]), n = bin.length, b = new Uint8Array(n);
    for (var i = 0; i < n; i++) b[i] = bin.charCodeAt(i);
    ctx.decodeAudioData(b.buffer, function (buf) {
      var ch = buf.numberOfChannels, N = buf.length, rate = buf.sampleRate;
      var mono = new Float32Array(N), t;
      for (var c = 0; c < ch; c++) { t = buf.getChannelData(c); for (var i = 0; i < N; i++) mono[i] += t[i] / ch; }
      var ab = new ArrayBuffer(44 + N * 2), v = new DataView(ab);
      function ws(o, s) { for (var i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); }
      ws(0, "RIFF"); v.setUint32(4, 36 + N * 2, true); ws(8, "WAVE");
      ws(12, "fmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
      v.setUint32(24, rate, true); v.setUint32(28, rate * 2, true);
      v.setUint16(32, 2, true); v.setUint16(34, 16, true);
      ws(36, "data"); v.setUint32(40, N * 2, true);
      for (var i = 0; i < N; i++) { var s = Math.max(-1, Math.min(1, mono[i])); v.setInt16(44 + i * 2, s < 0 ? s * 0x8000 : s * 0x7FFF, true); }
      var out = new Uint8Array(ab), str = "";
      for (var i = 0; i < out.length; i += 8192) str += String.fromCharCode.apply(null, out.subarray(i, i + 8192));
      res[k] = btoa(str); done++;
      if (done === keys.length) document.getElementById("out").textContent = "OK " + JSON.stringify(res);
    }, function (e) { document.getElementById("out").textContent = "FAIL " + k + " " + e; });
  });
} catch (e) { document.getElementById("out").textContent = "EX " + e; }
</script></body></html>""" % (",".join(payload),)

    hp = os.path.join(TMP, "dec.html")
    io.open(hp, "w", encoding="utf-8").write(html)
    dump = subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-first-run",
                           "--user-data-dir=" + os.path.join(TMP, "cr"),
                           "--virtual-time-budget=20000", "--dump-dom",
                           "file:///" + hp.replace("\\", "/")],
                          capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    m = re.search(r'OK (\{.*?\})</pre>', dump, re.S)
    if not m:
        print("   解码失败：", dump[:300])
        return 0
    got = json.loads(m.group(1))
    n = 0
    for i, (mp3, wav) in enumerate(items):
        b64 = got.get(str(i))
        if not b64:
            continue
        open(wav, "wb").write(base64.b64decode(b64))
        n += 1
    return n


PAUSE_MS = 300        # 英文站名前插入的静音长度（ms）
PAUSE_AFTER_NAME = 100   # 英文播报里“中文站名”和后面英文之间的小间隔（ms）


def _read_wav_mono(path):
    """读 WAV，多声道下混成单声道"""
    w = wave.open(path, "rb")
    rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
    a = array.array("h"); a.frombytes(w.readframes(n)); w.close()
    if ch > 1:
        a = array.array("h", [a[i] for i in range(0, len(a), ch)])
    return a, rate


def _trim_silence(a, rate, thr=180, pad_ms=25):
    """切掉首尾多余的静音（保留一点 pad）"""
    nz = [i for i, v in enumerate(a) if abs(v) > thr]
    if not nz:
        return a
    pad = int(rate * pad_ms / 1000.0)
    return a[max(0, nz[0] - pad): min(len(a), nz[-1] + pad)]


def split_name(s):
    """拆站名：前半中文、后半英文词。
       '滦河 Railway Station' -> ('滦河','Railway Station')；'邵家营村' -> ('邵家营村','')；
       'Transport Hub' -> ('','Transport Hub')"""
    for i, ch in enumerate(s):
        if ch.isascii() and ch.isalnum():
            return s[:i].strip(), s[i:].strip()
    return s.strip(), ""


def join_wavs(parts, out, gaps_ms=None):
    """把多段 TTS 拼成一个 WAV；段间插静音（每段首尾多余静音先切掉）。
       gaps_ms: 每两段之间的静音长度列表（不传就都按 PAUSE_MS）"""
    if gaps_ms is None:
        gaps_ms = [PAUSE_MS] * (len(parts) - 1)
    seg0, rate = _read_wav_mono(parts[0])
    buf = array.array("h", _trim_silence(seg0, rate))
    for idx, p in enumerate(parts[1:]):
        g = gaps_ms[idx] if idx < len(gaps_ms) else PAUSE_MS
        if g > 0:
            buf.extend(array.array("h", bytes(int(rate * g / 1000.0) * 2)))
        a, _ = _read_wav_mono(p)
        buf.extend(_trim_silence(a, rate))
    with wave.open(out, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(buf.tobytes())


def main():
    force = "--force" in sys.argv        # 带 --force 就全部重生成（文本改了以后用）
    os.makedirs(VOICE_DIR, exist_ok=True)
    os.makedirs(TMP, exist_ok=True)
    o = json.load(io.open(LINE_JSON, encoding="utf-8"))

    # 顺手改站名：万家 → 万嘉
    for s in o["stations"]:
        if "万家" in s["name"]:
            s["name"] = s["name"].replace("万家", "万嘉")
    io.open(LINE_JSON, "w", encoding="utf-8").write(json.dumps(o, ensure_ascii=False, indent=2) + "\n")

    st = o["stations"]
    total = len(st)
    jobs = []          # (文件名, 文本, 音色)

    for i, s in enumerate(st, 1):
        left = (s.get("door") == "left")
        sz = "左侧" if left else "右侧"
        se = "left" if left else "right"
        sec = "Left" if left else "Right"
        term = (i == 1 or i == total)
        num = "%02d" % i
        xf = s.get("transfers") or []
        xzh = ("可换乘" + "、".join(xf) + "。") if xf else ""
        xen = ("Transfer to " + ", ".join("Line " + t.replace("号线", "") for t in xf) + ".") if xf else ""
        # 英文播报里“站名”怎么念：line.json 里 enSay 优先（把拼音换成中文，英文词原样），没写就用 en
        en_name = s.get("enSay") or s["en"]
        # 中文：站名前留停顿靠中文逗号（≈0.25s）
        # 英文：拆成【英文引语】+【中文站名(中文女声)】+【英文后半句(英文男声)】，段间插静音拼起来
        #   （站名本身也可能中英混，比如 “滦河 Railway Station”，中文段、英文段各用各的嗓子）
        # term = 线路端点站（i==1 或 i==total）：开往它的【行驶中/即将到站】都要报“终点站”+“请全体乘客下车”
        nm_zh, nm_en = split_name(en_name)
        tzh    = "终点站" if term else ""
        civ    = "电子设备请勿外放声音，请把爱心座椅让给有需要的人。"
        en_civ = "Please keep your electronic devices on silent, and offer your seat to those in need."

        run_zh = "列车正在行驶，下一站，%s%s，%s车门将会打开，%s%s%s" % (
            tzh, s["name"], sz,
            "请站稳扶好，请全体乘客下车。" if term else "请站稳扶好。", xzh, civ)
        arr_zh = "列车即将到达，%s%s，%s车门将会打开，%s%s" % (
            tzh, s["name"], sz,
            "请全体乘客下车。" if term else "请提前做好下车准备。", xzh)

        def en_job(lead, rest):
            """lead=英文引语；rest=站名【英文部分】之后的英文句子（不含站名）"""
            segs = [lead]
            if nm_zh:
                segs.append((nm_zh, VOICE_ZH))          # 中文站名 → 中文女声
            tail = ("%s. %s" % (nm_en, rest)) if nm_en else rest
            segs.append(tail.strip())                   # 英文后半句 → 英文男声
            return segs

        if term:
            en_run = en_job("This train is now running. Next station, terminus,",
                            "%s-side doors will open. All passengers please alight. %s" % (sec, en_civ))
            en_arr = en_job("Arriving at the terminus,",
                            "%s-side doors will open. All passengers please alight." % sec)
        else:
            en_run = en_job("This train is now running. Next station,",
                            "%s-side doors will open. %s%s" % (sec, (xen + " ") if xen else "", en_civ))
            en_arr = en_job("Arriving at,",
                            "%s-side doors will open. Please get ready to alight.%s" % (sec, (" " + xen) if xen else ""))

        # 普通“本站”报站：每一站都有（端点站作为“始发站”开门时也用它）
        en_stop = en_job("This is,",
                         "Please alight from the %s-side doors, and mind the gap.%s" % (se, (" " + xen) if xen else ""))

        en_gaps = [PAUSE_MS, PAUSE_AFTER_NAME] if nm_zh else None
        jobs += [
            ("run_zh_%s" % num, run_zh, VOICE_ZH),
            ("run_en_%s" % num, en_run, VOICE_EN, en_gaps),
            ("arr_zh_%s" % num, arr_zh, VOICE_ZH),
            ("arr_en_%s" % num, en_arr, VOICE_EN, en_gaps),
            ("stop_zh_%s" % num, "本站，%s，请从%s车门下车，先下后上，注意列车与站台之间的空隙。%s" % (s["name"], sz, xzh), VOICE_ZH),
            ("stop_en_%s" % num, en_stop, VOICE_EN, en_gaps),
        ]
        if term:
            # 首末两站额外再生成一份“终点站”版；实际播哪版由程序按【当前行驶方向】决定
            # （在端点站换向后开门 = 始发站，就该报普通“本站”，不能报“请全部下车”）
            jobs += [
                ("term_zh_%s" % num, "终点站，%s到了，请所有乘客下车，感谢乘坐%s。" % (s["name"], LINE_ZH), VOICE_ZH),
                ("term_en_%s" % num, en_job("Terminus,", "Please alight. Thank you for travelling with %s." % LINE_EN),
                 VOICE_EN, en_gaps),
            ]
    jobs += [
        ("close_zh", "车门即将关闭，请注意安全，请勿抢上抢下。", VOICE_ZH),
        ("close_en", "Doors are closing. Please mind your safety and keep clear of the doors.", VOICE_EN),
    ]

    print("共 %d 条语音" % len(jobs))
    todo = [j for j in jobs if force or not os.path.exists(os.path.join(VOICE_DIR, j[0] + ".wav"))]
    print("需要生成 %d 条（已有 %d 条）" % (len(todo), len(jobs) - len(todo)))

    mp3s = []          # (mp3, wav)；多段的一条会拆成 name.p0/.p1…
    multi = []         # (name, [part_wav, ...], gaps) 需要拼接的
    for k, job in enumerate(todo, 1):
        name, text, voice = job[0], job[1], job[2]
        gap_list = job[3] if len(job) > 3 else None
        raw = text if isinstance(text, list) else [text]
        parts = [(it[0], it[1]) if isinstance(it, tuple) else (it, voice) for it in raw]
        pw = []
        for j, (t, v) in enumerate(parts):
            tag = name if len(parts) == 1 else "%s.p%d" % (name, j)
            mp3 = os.path.join(TMP, tag + ".mp3")
            srt = os.path.join(TMP, tag + ".srt")   # 附带字幕（时间轴）方便校对
            subprocess.run([EDGE, "--voice", v, "--text", t,
                            "--write-media", mp3, "--write-subtitles", srt],
                           capture_output=True)
            if os.path.exists(mp3):
                wav = os.path.join(VOICE_DIR if len(parts) == 1 else TMP, tag + ".wav")
                mp3s.append((mp3, wav))
                pw.append(wav)
            else:
                print("   [X] 生成失败:", tag)
        if len(parts) > 1:
            multi.append((name, pw, gap_list))
        if k % 10 == 0:
            print("   已下载 %d/%d" % (k, len(todo)))

    print("转 WAV（Chrome 解码，每批 8 条）...")
    ok = 0
    for i in range(0, len(mp3s), 8):
        ok += wav24_from_mp3_batch(mp3s[i:i + 8], VOICE_DIR)
        print("   转好 %d/%d" % (ok, len(mp3s)))

    # 多段的：每段首尾静音切掉，中间插静音，拼成最终文件
    for name, pw, gap_list in multi:
        if pw and all(os.path.exists(p) for p in pw):
            join_wavs(pw, os.path.join(VOICE_DIR, name + ".wav"), gap_list)
            print("   拼接", name, "(%d 段)" % len(pw))

    print("完成：assets/voice/ 里现在有 %d 个 wav" % len([f for f in os.listdir(VOICE_DIR) if f.endswith(".wav")]))


if __name__ == "__main__":
    main()
