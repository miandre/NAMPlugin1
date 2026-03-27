import glob, zipfile, os, fileinput, string, sys, shutil

scriptpath = os.path.dirname(os.path.realpath(__file__))
projectpath = os.path.abspath(os.path.join(scriptpath, os.pardir))

IPLUG2_ROOT = "..\\..\\iPlug2"

sys.path.insert(0, os.path.join(scriptpath, IPLUG2_ROOT + "\\Scripts"))

from get_archive_name import get_archive_name

PRODUCT_NAME = "RE-AMP"
INSTALLER_NAME = PRODUCT_NAME + " Installer.exe"
DEMO_INSTALLER_NAME = PRODUCT_NAME + " Demo Installer.exe"
MANUAL_NAME = PRODUCT_NAME + " manual.pdf"


def main():
    if len(sys.argv) != 3:
        print("Usage: make_zip.py demo[0/1] zip[0/1]")
        sys.exit(1)
    else:
        demo = int(sys.argv[1])
        zip = int(sys.argv[2])

    dir = projectpath + "\\build-win\\out"

    if os.path.exists(dir):
        shutil.rmtree(dir)

    os.makedirs(dir)

    files = []

    if not zip:
        installer = "\\build-win\\installer\\" + INSTALLER_NAME

        if demo:
            installer = "\\build-win\\installer\\" + DEMO_INSTALLER_NAME

        files = [
            projectpath + installer,
            projectpath + "\\installer\\changelog.txt",
            projectpath + "\\installer\\known-issues.txt",
            projectpath + "\\manual\\" + MANUAL_NAME,
        ]
    else:
        files = [
            projectpath
            + "\\build-win\\"
            + PRODUCT_NAME
            + ".vst3\\Contents\\x86_64-win\\"
            + PRODUCT_NAME
            + ".vst3",
            projectpath + "\\build-win\\" + PRODUCT_NAME + "_x64.exe",
        ]

    zipname = get_archive_name(projectpath, "win", "demo" if demo == 1 else "full")

    zf = zipfile.ZipFile(
        projectpath + "\\build-win\\out\\" + zipname + ".zip", mode="w"
    )

    for f in files:
        print("adding " + f)
        zf.write(f, os.path.basename(f), zipfile.ZIP_DEFLATED)

    zf.close()
    print("wrote " + zipname)

    zf = zipfile.ZipFile(
        projectpath + "\\build-win\\out\\" + zipname + "-pdbs.zip", mode="w"
    )

    files = sorted(glob.glob(projectpath + "\\build-win\\pdbs\\*.pdb"))

    for f in files:
        print("adding " + f)
        zf.write(f, os.path.basename(f), zipfile.ZIP_DEFLATED)

    zf.close()
    print("wrote " + zipname)


if __name__ == "__main__":
    main()
