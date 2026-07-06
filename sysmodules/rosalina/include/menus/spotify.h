#pragma once

#include <3ds/types.h>
#include "menu.h"

extern Menu spotifyMenu;

typedef enum Spotify_Action {
    SPOTIFY_ACTION_STATUS = 0,
    SPOTIFY_ACTION_NEXT,
    SPOTIFY_ACTION_PREVIOUS,
    SPOTIFY_ACTION_PLAY,
    SPOTIFY_ACTION_PAUSE,
    SPOTIFY_ACTION_PLAYPAUSE,
} Spotify_Action;

void SpotifyMenu_Status(void);
void SpotifyMenu_Next(void);
void SpotifyMenu_Previous(void);
void SpotifyMenu_Play(void);
void SpotifyMenu_Pause(void);
void SpotifyMenu_PlayPause(void);
void SpotifyMenu_Controls(void);

void Spotify_HandleHotkeys(u32 heldKeys, u32 downKeys);
