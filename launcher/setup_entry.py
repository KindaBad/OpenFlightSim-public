import sys

if '--check-downloads' in sys.argv:
    from launcher.download_probe import main
else:
    from launcher.setup_ui import main

if __name__ == '__main__':
    raise SystemExit(main())
