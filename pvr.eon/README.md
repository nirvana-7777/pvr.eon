# PVR Demo Data
Dummy data and files to be used with the PVR Demo Add-on.

## Icons
There are 11 icons used for the channels. The same icons are used for TV and Radio.

## PVR Channel Data types

The relationship between Providers, Channels, Channel Groups and Recordings are as follow:
- *Providers*:
  - Are independent of TV or Radio. A PVR add-on can create custom providers, it is also a provider itself.
- *Channels*:
  - Can specify a provider ID. If one is not specified, its provider will default to the PVR add-on as the provider
- *Channel Groups*:
  - can contain either TV channels or Radio channels
- *Recordings*:
  - Can specify a channel ID and/or a provider ID. The fallback order is `Channel -> Custom Provider -> Addon provider`.

### Channels and Providers behaviour for Recordings

#### /Directory1/
The first four TV and Radio recordings (1-4) have a channel ID set as per PVR API 5.0.0. For these recordings the channel name and icon should be visible when inspecting the recordings in the UI. They also have provider IDs set, but as this is a fallback used in the absence of Channel IDs, the provider will not be visible.

#### /Directory2/
The next 3 TV and Radio recordings (5-7) have a provider ID set as per PVR API 8.0.0. Neither channel name or channel ID are set. For these recordings the provider name and icon (custom provider used) should be visible when inspecting the recordings in the UI.

#### /Directory3/
The next 3 TV and Radio recordings (8-10) don't have a channel ID or a provider ID set, but do have channel name set. For these recordings the PVR add-on name and icon (add-on provider used) should be visible when inspecting the recordings in the UI.