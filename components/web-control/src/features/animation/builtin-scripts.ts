export const builtinScripts = {
  ripple: `# Ripple: expanding wave then contracting
for-dist
  add dist:$d
  frame
end
for-dist-rev
  remove dist:$d
  frame
end`,

  band: `# Band: traveling pair of adjacent layers
set dist:0
frame
for-dist 1
  set dist:$d-1..$d
  frame
  set dist:$d
  frame
end
for-dist-rev $max-1 0
  set dist:$d..$d+1
  frame
  set dist:$d
  frame
end`,

  chase: `# Chase: walk around each distance ring
cursor center
for-dist 1
  for-node dist:$d sort angle
    walk-to $n constrain dist:$d-1..$d
  end
end
walk-to center`,

  walker: `# Walker: depth-first graph traversal
cursor center
dfs sort shell-angle`,

  "ripple-outline": `# Ripple (outlines only)
filter-scene type:vertex
for-dist
  add dist:$d
  frame
end
for-dist-rev
  remove dist:$d
  frame
end`,

  "band-outline": `# Band (outlines only)
filter-scene type:vertex
set dist:0
frame
for-dist 1
  set dist:$d-1..$d
  frame
  set dist:$d
  frame
end
for-dist-rev $max-1 0
  set dist:$d..$d+1
  frame
  set dist:$d
  frame
end`,

  "chase-outline": `# Chase (outlines only)
filter-scene type:vertex
cursor center
for-dist 1
  for-node dist:$d sort angle
    walk-to $n constrain dist:$d-1..$d
  end
end
walk-to center`,

  "walker-outline": `# Walker (outlines only)
filter-scene type:vertex
cursor center
dfs sort shell-angle`,
};
