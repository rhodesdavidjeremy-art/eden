from __future__ import annotations

import base64
import hashlib
import math
import os
import re
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional

from .config import ImageConfig
from .errors import ConfigError, InvalidRequestError, ProviderError
from .http import HttpClient
from .models import CancelToken, PackContext
from .secrets import SecretStore

STYLE_PRESETS: Dict[str, str] = {
    "jewel": "illuminated jewel-toned artwork, ruby, emerald, sapphire and amethyst glass-like facets, luminous gold "
             "filigree, deep velvet background, soft glow",
    "symbolic": "symbolic allegorical illustration, rich metaphor, balanced composition, painterly texture, "
                "warm gold highlights",
    "realistic": "photorealistic scene, natural lighting, high detail, cinematic framing",
    "diagrammatic": "clean explanatory diagram, flat vector shapes, clear hierarchy, muted jewel palette, no text",
    "eden": "mystical garden of Eden at dusk, a golden river of light flowing between ancient trees, crimson clouds "
            "with faint lightning, floating golden particles, reverent atmosphere",
}

NO_TEXT_SUFFIX = "Do not render any words, letters, captions, or numbers."


@dataclass
class GeneratedImage:
    path: str
    prompt: str
    style: str
    sha256: str
    cached: bool

    def to_dict(self) -> dict:
        return {"path": self.path, "prompt": self.prompt, "style": self.style, "sha256": self.sha256,
                "cached": self.cached}


def build_image_prompt(base_prompt: str, style: str) -> str:
    preset = STYLE_PRESETS.get(style)
    if preset is None:
        raise InvalidRequestError(f"unknown image style: {style}")
    base = re.sub(r"\s+", " ", base_prompt).strip()
    if not base:
        raise InvalidRequestError("image prompt is empty")
    return f"{base}. Style: {preset}. {NO_TEXT_SUFFIX}"


def plan_scene_count(text: str, maximum: int = 6) -> int:
    return max(1, min(maximum, int(math.ceil(len(text.strip()) / 450.0))))


def split_scenes(text: str, count: int) -> List[str]:
    sentences = [s for s in re.split(r"(?<=[.!?])\s+", text.strip()) if s]
    if not sentences:
        return []
    count = max(1, min(count, len(sentences)))
    size = int(math.ceil(len(sentences) / count))
    return [" ".join(sentences[i:i + size]) for i in range(0, len(sentences), size)]


class ImageService:
    def __init__(self, config: ImageConfig, secrets: SecretStore, *, secret_name: str = "images",
                 http: Optional[HttpClient] = None) -> None:
        self.config = config
        self.secrets = secrets
        self.secret_name = secret_name
        self.http = http or HttpClient(timeout=config.timeout, max_retries=1)
        if not config.cache_dir:
            raise ConfigError("image cache directory is not configured")
        os.makedirs(config.cache_dir, exist_ok=True)

    def _cache_path(self, prompt: str, style: str, size: str) -> str:
        digest = hashlib.sha256(f"{self.config.model}\x00{size}\x00{style}\x00{prompt}".encode("utf-8")).hexdigest()
        return os.path.join(self.config.cache_dir, digest + ".png")

    def generate(self, base_prompt: str, style: str = "jewel", *, size: str = "",
                 refresh: bool = False, cancel: Optional[CancelToken] = None) -> GeneratedImage:
        prompt = build_image_prompt(base_prompt, style)
        size = size or self.config.size
        path = self._cache_path(prompt, style, size)
        if os.path.exists(path) and not refresh:
            with open(path, "rb") as handle:
                data = handle.read()
            return GeneratedImage(path, prompt, style, hashlib.sha256(data).hexdigest(), True)
        key = self.secrets.get(self.secret_name)
        if not key:
            raise ConfigError("no api key configured for image generation")
        payload = {"model": self.config.model, "prompt": prompt, "size": size, "n": 1}
        data = self.http.request_json("POST", self.config.base_url.rstrip("/") + "/images/generations",
                                      headers={"Authorization": "Bearer " + key}, payload=payload,
                                      provider="images", cancel=cancel)
        items = data.get("data") or []
        if not items:
            raise ProviderError("image provider returned no images", provider="images")
        item = items[0]
        if item.get("b64_json"):
            raw = base64.b64decode(item["b64_json"])
        elif item.get("url"):
            _, raw = self.http.request("GET", str(item["url"]), provider="images", cancel=cancel)
        else:
            raise ProviderError("image response had neither data nor url", provider="images")
        temporary = path + ".part"
        with open(temporary, "wb") as handle:
            handle.write(raw)
        os.replace(temporary, path)
        return GeneratedImage(path, prompt, style, hashlib.sha256(raw).hexdigest(), False)

    def generate_set(self, ctx: PackContext, scene_prompts: List[str], style: str = "jewel",
                     cancel: Optional[CancelToken] = None,
                     progress: Optional[Callable[[int, int], None]] = None) -> List[GeneratedImage]:
        images: List[GeneratedImage] = []
        for index, prompt in enumerate(scene_prompts):
            if cancel is not None:
                cancel.check()
            images.append(self.generate(prompt, style, cancel=cancel))
            if progress is not None:
                progress(index + 1, len(scene_prompts))
        return images
