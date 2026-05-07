Import("env")

if (env.subst("$UPLOAD_PROTOCOL") or "picotool") == "picotool":
    env.Append(UPLOADERFLAGS=["-F"])
