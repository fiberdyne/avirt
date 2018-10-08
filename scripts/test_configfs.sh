#!/bin/bash

mkdir -p /config && mount -t configfs none /config

mkdir /config/avirt/streams/playback_media
echo "2">/config/avirt/streams/playback_media/channels
echo "ap_loopback">/config/avirt/streams/playback_media/map

mkdir /config/avirt/streams/playback_navigation
echo "1">/config/avirt/streams/playback_navigation/channels
echo "ap_loopback">/config/avirt/streams/playback_navigation/map

mkdir /config/avirt/streams/playback_emergency
echo "1">/config/avirt/streams/playback_emergency/channels
echo "ap_loopback">/config/avirt/streams/playback_emergency/map

mkdir /config/avirt/streams/capture_voice
echo "1">/config/avirt/streams/capture_voice/channels
echo "ap_loopback">/config/avirt/streams/capture_voice/map

echo "1">/config/avirt/streams/sealed
