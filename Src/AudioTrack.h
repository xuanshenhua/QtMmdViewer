#ifndef AUDIOTRACK_H
#define AUDIOTRACK_H

#include <QObject>
#include <QString>

#ifdef QtMmdHasMultimedia
class QMediaPlayer;
class QAudioOutput;
#endif

// 音频轨道：封装 Qt Multimedia 的 QMediaPlayer。
// 若编译时没有 Qt Multimedia 模块，则所有接口为空实现，程序仍可正常运行（无声音）。
class AudioTrack : public QObject
{
    Q_OBJECT

public:
    explicit AudioTrack(QObject* Parent = nullptr);
    ~AudioTrack() override;

    static bool IsSupported();
    bool Load(const QString& FilePath, QString* ErrorMessage);
    void Unload();
    bool HasAudio() const { return Loaded; }
    QString FilePath() const { return CurrentPath; }

    void Play();
    void Pause();
    void Stop();
    void Seek(double Seconds);
    bool IsPlaying() const;
    double PositionSeconds() const;
    double DurationSeconds() const;
    void SetVolume(float Volume);
    void SetRate(float Rate);

signals:
    void DurationChanged(double Seconds);

private:
#ifdef QtMmdHasMultimedia
    QMediaPlayer* Player;
    QAudioOutput* Output;
#endif
    bool Loaded;
    QString CurrentPath;
};

#endif
