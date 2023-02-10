import os.path
import shutil
import gzip
import subprocess


if shutil.which("npx") is not None:
    def read(filename):
        return subprocess.check_output(["npx", "minify", filename], shell=True)
else:
    def read(filename):
        with open(filename, "rb") as f:
            return f.read()


os.makedirs("data", exist_ok=True)

with open("data/index.html", 'wb') as f:
    f.write(read("web/index.html"))

with gzip.open('data/style.css.gz', 'wb') as f:
    f.write(read("web/style.css"))
