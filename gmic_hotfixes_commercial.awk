# The hot fixes of COMMERCIAL builds (gmic_hotfixes.gmic on stdin or as the file argument, the
# kept lines on stdout), for both the top-level Makefile and CMakeLists.txt. These builds only
# keep the fixes of the commands of their standard library (the others fix code under a licence
# that they leave out, see gmic_stdlib_commercial.txt): a block
# "#@hotfix <command> <original> <fixed>[,<other>...] [callee:checksum...]" is left out when its
# command (not when <original> is "-": G'MIC no longer defines it) or one of its callees is not in
# the file
# "cmds" (gmic_stdlib_community.h.commands, one command per line, from gmic_stdlib_commercial.sh).
#   awk -v cmds=<commands file> -f gmic_hotfixes_commercial.awk gmic_hotfixes.gmic
BEGIN { while ((getline c < cmds) > 0) def[c] = 1; keep = 1 }
{ sub(/\r$/, "") }
/^#@hotfix / {
  keep = 1
  for (i = 2; i <= NF; i++) if ((i == 2 && $3 != "-") || i > 4) { c = $i; sub(/:.*/, "", c); if (!(c in def)) keep = 0 }
  if (!keep) print "gmic_hotfixes.h: leaving out the fix of " $2 " (not in the COMMERCIAL standard library)" > "/dev/stderr"
}
keep { print }
