#include "AudioTrack.h"

#include <QFileInfo>
#include <QUrl>

#ifdef QtMmdHasMultimedia
#include <QAudioOutput>
#include <QMediaPlayer>
#endif

AudioTrack::AudioTrack(QObject* Parent)
    : QObject(Parent), Loaded(false)
{
#ifdef QtMmdHasMultimedia
    Player = new QMediaPlayer(this);
    Output = new QAudioOutput(this);
    Player->setAudioOutput(Output);
    Output->setVolume(0.5f);
    connect(Player, &QMediaPlayer::durationChanged, this, [this](qint64 Duration) {
        emit DurationChanged(double(Duration) / 1000.0);
    });
#endif
}

AudioTrack::~AudioTrack()
{
}

bool AudioTrack::IsSupported()
{
#ifdef QtMmdHasMultimedia
    return true;
#else
    return false;
#endif
}

bool AudioTrack::Load(const QString& Path, QString* ErrorMessage)
{
    Unload();
    if (!QFileInfo::exists(Path)) {
        if (ErrorMessage) {
            *ErrorMessage = QStringLiteral("音频文件不存在：%1").arg(Path);
        }
        return false;
    }
#ifdef QtMmdHasMultimedia
    Player->setSource(QUrl::fromLocalFile(Path));
    Loaded = true;
    CurrentPath = Path;
    return true;
#else
    if (ErrorMessage) {
        *ErrorMessage = QStringLiteral("当前程序编译时未启用 Qt Multimedia 模块，无法播放音频。");
    }
    return false;
#endif
}

void AudioTrack::Unload()
{
#ifdef QtMmdHasMultimedia
    Player->stop();
    Player->setSource(QUrl());
#endif
    Loaded = false;
    CurrentPath.clear();
}

void AudioTrack::Play()
{
#ifdef QtMmdHasMultimedia
    if (Loaded) {
        Player->play();
    }
#endif
}

void AudioTrack::Pause()
{
#ifdef QtMmdHasMultimedia
    if (Loaded) {
        Player->pause();
    }
#endif
}

void AudioTrack::Stop()
{
#ifdef QtMmdHasMultimedia
    if (Loaded) {
        Player->stop();
    }
#endif
}

void AudioTrack::Seek(double Seconds)
{
#ifdef QtMmdHasMultimedia
    if (Loaded) {
        Player->setPosition(qint64(Seconds * 1000.0));
    }
#else
    Q_UNUSED(Seconds);
#endif
}

bool AudioTrack::IsPlaying() const
{
#ifdef QtMmdHasMultimedia
    return Loaded && Player->playbackState() == QMediaPlayer::PlayingState;
#else
    return false;
#endif
}

double AudioTrack::PositionSeconds() const
{
#ifdef QtMmdHasMultimedia
    return Loaded ? double(Player->position()) / 1000.0 : 0.0;
#else
    return 0.0;
#endif
}

double AudioTrack::DurationSeconds() const
{
#ifdef QtMmdHasMultimedia
    return Loaded ? double(Player->duration()) / 1000.0 : 0.0;
#else
    return 0.0;
#endif
}

void AudioTrack::SetVolume(float Volume)
{
#ifdef QtMmdHasMultimedia
    Output->setVolume(Volume);
#else
    Q_UNUSED(Volume);
#endif
}

void AudioTrack::SetRate(float Rate)
{
#ifdef QtMmdHasMultimedia
    Player->setPlaybackRate(Rate);
#else
    Q_UNUSED(Rate);
#endif
}
