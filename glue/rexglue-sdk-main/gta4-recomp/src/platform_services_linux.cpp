/**
 * @file        platform_services_linux.cpp
 * @brief       Linux counterparts of the Apple-only platform services
 *
 * The macOS app implements these in Objective-C++ (achievement_bridge_gc.mm,
 * input/user_music_player.mm, network/gta4_microphone_permission.mm). Linux
 * has no Game Center, no Music library access and no microphone permission
 * prompt, so achievements are dropped, no user music is offered and the
 * microphone is always available to the voice chat path.
 */
#include "achievement_bridge_gc.h"
#include "input/user_music_player.h"
#include "network/gta4_microphone_permission.h"

#include <atomic>
#include <utility>

namespace gta4::game_center {

void Initialize() {}

void SubmitAchievement(uint32_t xbox_id) {
  (void)xbox_id;
}

}  // namespace gta4::game_center

namespace gta4::input {

struct UserMusicPlayer::Impl {};

UserMusicPlayer::UserMusicPlayer(std::filesystem::path music_root)
    : impl_(std::make_unique<Impl>()) {
  (void)music_root;
}

UserMusicPlayer::~UserMusicPlayer() = default;

bool UserMusicPlayer::Next() {
  return false;
}

bool UserMusicPlayer::Previous() {
  return false;
}

bool UserMusicPlayer::Stop() {
  return false;
}

bool UserMusicPlayer::HasTracks() const {
  return false;
}

namespace {
std::atomic<UserMusicPlayer*> published_player{nullptr};
}  // namespace

void PublishUserMusicPlayer(UserMusicPlayer* player) {
  published_player.store(player, std::memory_order_release);
}

bool IsUserMusicAvailable() {
  return false;
}

bool RequestUserMusicNext() {
  return false;
}

bool RequestUserMusicPrevious() {
  return false;
}

bool RequestUserMusicStop() {
  return false;
}

}  // namespace gta4::input

namespace gta4::voice {

MicrophonePermissionStatus GetMicrophonePermissionStatus() noexcept {
  return MicrophonePermissionStatus::kAuthorized;
}

void RequestMicrophonePermission(std::function<void()> completion) {
  if (completion) {
    completion();
  }
}

}  // namespace gta4::voice
