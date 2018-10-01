#!/bin/bash

mkdir -p /config && mount -t configfs none /config

mkdir /config/avirt/streams/playback_media
echo "2">/config/avirt/streams/playback_media/channels

mkdir /config/avirt/streams/playback_navigation
echo "1">/config/avirt/streams/playback_navigation/channels

mkdir /config/avirt/streams/playback_emergency
echo "1">/config/avirt/streams/playback_emergency/channels

mkdir /config/avirt/streams/capture_voice
echo "1">/config/avirt/streams/capture_voice/channels

echo "1">/config/avirt/streams/sealed
