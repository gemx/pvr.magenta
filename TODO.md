# Refactoring
* Split GetPostJson into seperate calls because POST never returns a body
* Have a parameter for the cache lifetime in GetJson to let the caller decide about the TTL
* Make it Piers compatible

# Channels and Channelgroups
* have only "all channels" and "favorites" as groups because you can only change these groups via the magenta app

# Authentication
* fix auth for Magenta2. Currently after an install you have to switch to Magenta1, exit Kodi, start kodi, authenticate, switch to Magenta2, exit kodi, start kodi, authenticate
  This seems a bit broken