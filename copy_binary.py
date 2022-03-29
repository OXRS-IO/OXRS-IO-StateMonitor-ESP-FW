Import('env')

import os
import shutil

board = env.GetProjectOption("board")
config = env.GetProjectConfig()

firmware_name = config.get("firmware", "name")
firmware_version = config.get("firmware", "version")

binary_name = "%s_%s_v%s" % (firmware_name, board, firmware_version)
binary_path = os.path.join(os.getcwd(), "binaries", firmware_version)

def copy_binary(source, target, env):
    print("Copying binary to {0}".format(binary_path))
    os.makedirs(binary_path, exist_ok=True)
    shutil.copy(str(source[0]), binary_path)

env.Replace(PROGNAME=binary_name, UPLOADCMD=copy_binary)