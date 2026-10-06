import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import re
from .config import user_directory


class Redact(logging.Filter):
    def filter(self, record):
        # URLs may contain CDN query tokens. Diagnostics never record them.
        text = record.getMessage()
        text = re.sub(r'https?://[^\s]+', '[HTTPS endpoint]', text)
        text = re.sub(r'(?i)(token|password|secret|authorization)\s*[:=]\s*\S+', r'\1=[redacted]', text)
        record.msg, record.args = text, ()
        return True


def configure_logs(directory=None, name='launcher'):
    directory = Path(directory or user_directory() / 'logs')
    directory.mkdir(parents=True, exist_ok=True)
    logger = logging.getLogger('ofs')
    logger.setLevel(logging.INFO)
    handler = RotatingFileHandler(directory / f'{name}.log', maxBytes=2 * 1024 * 1024, backupCount=3, encoding='utf-8')
    handler.addFilter(Redact())
    handler.setFormatter(logging.Formatter('%(asctime)s %(levelname)s %(name)s: %(message)s'))
    logger.addHandler(handler)
    return directory
