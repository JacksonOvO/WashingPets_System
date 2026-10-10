"""Render comparable 1400 x 900 page images without connecting to hardware.

python -m windows.preview --backend qt --output windows/previews/qt
python -m windows.preview --backend gtk --output windows/previews/linux
GTK capture runs on the Linux development machine only.
"""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=['qt', 'gtk'], default='qt')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    from ui.i18n import set_lang
    set_lang('zh')
    if args.backend == 'qt':
        from PySide6.QtWidgets import QApplication
        from windows.main import WorkbenchWindow
        from windows.theme import STYLE
        app = QApplication([])
        app.setStyle('Fusion')
        app.setStyleSheet(STYLE)
        window = WorkbenchWindow()
        window.timer.stop()
        window.poll_timer.stop()
        window.show()
        for index, key in enumerate(window.PAGE_KEYS):
            window.show_page(index)
            window.refresh()
            for _ in range(20):
                app.processEvents()
            if not window.grab().save(str(args.output / (key + '.png'))):
                raise RuntimeError('Screenshot save failed: ' + key)
        window.close()
    else:
        import cairo
        import gi
        gi.require_version("Gtk", "3.0")
        from gi.repository import Gtk, GLib
        from gtk_app import WorkbenchWindow
        window = WorkbenchWindow()
        window.set_decorated(False)
        window.stack.set_transition_type(Gtk.StackTransitionType.NONE)
        window.show_all()
        keys = iter(['dashboard', 'telemetry', 'faults', 'control', 'io', 'params', 'test', 'upgrade'])
        def show_next():
            try:
                key = next(keys)
            except StopIteration:
                window.destroy()
                Gtk.main_quit()
                return False
            window._on_nav(window.sidebar, key)
            window._refresh()
            GLib.timeout_add(300, save, key)
            return False
        def save(key):
            surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, 1400, 900)
            window.draw(cairo.Context(surface))
            surface.write_to_png(str(args.output / (key + '.png')))
            GLib.timeout_add(20, show_next)
            return False
        GLib.timeout_add(500, show_next)
        Gtk.main()


if __name__ == '__main__':
    main()
