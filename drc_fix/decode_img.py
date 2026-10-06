import base64, sys
# reads b64 from stdin file arg
data = open(sys.argv[1]).read().strip().replace("\n", "")
open(sys.argv[2], "wb").write(base64.b64decode(data))
print("written", len(data))
