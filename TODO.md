# Refactoring
* Split GetPostJson into seperate calls because POST never returns a body
* Have a parameter for the cache lifetime in GetJson to let the caller decide about the TTL
* Make it Piers compatible
