# Custom music and audio packs

1. Start the game and open **F1 → Audio**. Enable **Use custom music** and
   select **Open music packs**. Use the folder opened by this button: it belongs
   to the executable you are running.
2. Open `Tracks.txt` in that folder. It lists the menu and stage music names
   on your ISO. If the list is missing, open the folder again while the game
   is running with its ISO loaded.
3. Pick a listed track. For example, if it says `/audio/name.hps`, create the
   `audio` folder inside `MusicPacks` and put your replacement there as
   `name.mp3`, `name.wav`, `name.m4a`, `name.flac`, `name.ogg`, or `name.hps`.
   Replace `name` with the actual name from `Tracks.txt`; keep the extension
   matching the file's format. Renaming an MP3 to `.hps` does not convert it.
4. Leave and re-enter the stage or menu so the song starts again. Replacing a
   file does not restart the song that is already playing.

For a random playlist, put several supported files with any names inside
`MusicPacks/audio/name/`, using the original track's name for the folder.
The game chooses a song each time that original track starts. A direct
replacement such as `MusicPacks/audio/name.mp3` takes priority over the playlist.

One playlist can serve several tracks, such as a stage's main and alternate
songs: name the folder after each of them joined with `+`, for example
`MusicPacks/audio/name+othername/`. A folder named after one track alone is
used for that track first.

Extract downloaded ZIP packs first. The final path must be
`MusicPacks/audio/...`, without an extra pack-name folder between them.
These packs replace music; they do not replace character voices or sound effects.

If the original music still plays, check **Use custom music**, the Music and
Volume sliders, the exact name in `Tracks.txt`, and the folder opened from the
currently running build. Vanilla audio mode uses the original disc music.
An unsupported or damaged replacement falls back to the disc track; see
`melee_port.log` for messages beginning `jukebox:`. Windows uses its installed
Media Foundation decoders, so codec support can vary. HPS must be stereo;
ordinary decoded audio is limited to ten minutes per track.

Music packs play locally and do not require your online opponent to install them.
