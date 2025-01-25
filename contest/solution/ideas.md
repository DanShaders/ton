# Potential places to optimize




## OutMessageQueue combinations


## ? Everything "loopy"

## ? Everything "map"y
into unordered_map. Seems to not improve performance significantly.
Or maybe even to third-party map data structures:
- https://github.com/martinus/unordered_dense



## inline stuff




## Optimize positive-path instruction density

Convert:
```
if a goto aGood
	processBadACase1
	processBadACase2
	processBadACase3
	return
aGood:
if b goto bGood
	processBadBCase1
	processBadBCase2
	return
bGood:
```
into:
```
if !a goto aBad
if !b goto bBad

aBad:
	processBadACase1
	processBadACase2
	processBadACase3
	return
bBad:
	processBadBCase1
	processBadBCase2
	return
```

Replace `  return reject`
by `  [[unlikely]] return reject`
doesn't improve performance (at least substantially);


## Optimize common primitives

- dict.cpp
- Cell, Slice, etc.



## Compiler auto optimizations?
-fprofile-generate
-fprofile-use
https://stackoverflow.com/a/10755690



# Resources

Compiler stuff:
- https://stackoverflow.com/questions/30130930/is-there-a-compiler-hint-for-gcc-to-force-branch-prediction-to-always-go-a-certa
- https://en.cppreference.com/w/cpp/language/attributes/likely
- https://kernelnewbies.org/FAQ/LikelyUnlikely




# Might be usefull, will check out later
- https://kernelnewbies.org/FAQ/down

