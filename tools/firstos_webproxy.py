#!/usr/bin/env python3
"""Small host-side HTTPS bridge for the FirstOS QEMU network.

The guest browser speaks ordinary HTTP to 10.0.2.2:18080 and places the
original URL in X-FirstOS-Target.  urllib performs TLS, redirects, and normal
HTTP response handling on the host; FirstOS still parses and renders the
returned document itself.
"""

from __future__ import annotations

import argparse
import sys
import urllib.error
import urllib.parse
import urllib.request
import ssl
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


try:
    import certifi
    ssl_context = ssl.create_default_context(cafile=certifi.where())
except Exception:
    ssl_context = ssl.create_default_context()
    ssl_context.check_hostname = False
    ssl_context.verify_mode = ssl.CERT_NONE

# Create an unverified fallback opener in case certificates fail verification on macOS
unverified_context = ssl.create_default_context()
unverified_context.check_hostname = False
unverified_context.verify_mode = ssl.CERT_NONE

import time
import threading
import html
import json
import re

MAX_TARGET = 4096
MAX_REQUEST_BODY = 8 * 1024 * 1024
MAX_RESPONSE_BODY = 32 * 1024 * 1024

_STATIC_CACHE: dict[str, tuple[int, dict[str, str], bytes]] = {}
_CACHE_LOCK = threading.Lock()


def _search_web_results(query: str) -> list[dict[str, str]]:
    results = []
    # 1. Try DuckDuckGo HTML
    try:
        req = urllib.request.Request(
            "https://html.duckduckgo.com/html/",
            data=urllib.parse.urlencode({"q": query}).encode(),
            headers={
                "User-Agent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:128.0) Gecko/20100101 Firefox/128.0",
                "Content-Type": "application/x-www-form-urlencoded",
                "Accept": "text/html,application/xhtml+xml",
                "Referer": "https://html.duckduckgo.com/"
            }
        )
        with urllib.request.urlopen(req, timeout=8, context=unverified_context) as r:
            content = r.read().decode("utf-8", "replace")
        for m in re.finditer(r"<a rel=\"nofollow\" class=\"result__a\" href=\"([^\"]+)\">(.*?)</a>.*?<a class=\"result__snippet\"[^>]*>(.*?)</a>", content, re.S):
            href, title, snip = m.group(1), m.group(2), m.group(3)
            t_clean = html.unescape(re.sub(r"<[^>]+>", "", title).strip())
            s_clean = html.unescape(re.sub(r"<[^>]+>", "", snip).strip())
            if href.startswith("//duckduckgo.com/l/?uddg="):
                u = urllib.parse.unquote(href.split("uddg=")[1].split("&")[0])
            else:
                u = href
            if t_clean and u:
                results.append({"url": u, "title": t_clean, "snippet": s_clean})
        if results:
            return results
    except Exception:
        pass

    # 2. Fallback to Bing
    try:
        b_url = f"https://www.bing.com/search?q={urllib.parse.quote_plus(query)}"
        req = urllib.request.Request(b_url, headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"})
        with urllib.request.urlopen(req, timeout=8, context=unverified_context) as r:
            content = r.read().decode("utf-8", "replace")
        for m in re.finditer(r"<li class=\"b_algo\"><h2><a href=\"([^\"]+)\"[^>]*>(.*?)</a></h2>.*?<div class=\"b_caption\"><p>(.*?)</p>", content, re.S):
            u, t, s = m.group(1), m.group(2), m.group(3)
            t_clean = html.unescape(re.sub(r"<[^>]+>", "", t).strip())
            s_clean = html.unescape(re.sub(r"<[^>]+>", "", s).strip())
            if t_clean and u:
                results.append({"url": u, "title": t_clean, "snippet": s_clean})
        if results:
            return results
    except Exception:
        pass

    # 3. Fallback to Wikipedia API
    try:
        w_url = f"https://en.wikipedia.org/w/api.php?action=opensearch&search={urllib.parse.quote_plus(query)}&limit=8&namespace=0&format=json"
        req = urllib.request.Request(w_url, headers={"User-Agent": "FirstOS-Browser/2.1"})
        with urllib.request.urlopen(req, timeout=6, context=unverified_context) as r:
            data = json.loads(r.read().decode("utf-8", "replace"))
        if len(data) >= 4:
            titles = data[1]
            descriptions = data[2]
            urls = data[3]
            for i in range(len(titles)):
                results.append({"url": urls[i], "title": titles[i], "snippet": descriptions[i] or f"Learn more about {titles[i]} on Wikipedia."})
    except Exception:
        pass

    return results


def _render_google_search_html(query: str, results: list[dict[str, str]]) -> bytes:
    q_esc = html.escape(query)
    items_html = ""
    for r in results:
        t = html.escape(r["title"])
        u = html.escape(r["url"])
        s = html.escape(r["snippet"])
        items_html += f"""
        <div class="g">
          <div class="cite"><a href="{u}">{u}</a></div>
          <h3><a href="{u}">{t}</a></h3>
          <div class="snippet">{s}</div>
        </div>"""

    if not items_html:
        items_html = f"""<div class="g"><p>No search results found for <b>{q_esc}</b>.</p><p>Try checking your spelling or using more general terms.</p></div>"""

    page = f"""<!doctype html><html><head><title>{q_esc} - Google Search</title>
    <style>
      body{{font-family:sans-serif;margin:0;padding:20px 32px;background:#ffffff;color:#202124;}}
      .hdr{{display:flex;align-items:center;margin-bottom:18px;}}
      .logo{{font-size:24px;font-weight:bold;color:#4285F4;margin-right:24px;text-decoration:none;}}
      input{{width:480px;height:34px;padding:4px 14px;border:1px solid #dfe1e5;border-radius:24px;font-size:16px;background:#fff;color:#000;}}
      button{{background:#1a73e8;color:#fff;border:none;padding:8px 18px;border-radius:4px;font-weight:bold;margin-left:10px;cursor:pointer;}}
      .tabs{{display:flex;gap:18px;border-bottom:1px solid #ebebeb;padding-bottom:10px;margin-bottom:18px;font-size:14px;color:#5f6368;}}
      .tabs a{{color:#5f6368;text-decoration:none;font-weight:bold;margin-right:12px;}}
      .tabs .act{{color:#1a73e8;border-bottom:3px solid #1a73e8;padding-bottom:8px;}}
      .g{{margin-bottom:24px;max-width:680px;}}
      .cite a{{color:#202124;font-size:13px;text-decoration:none;word-break:break-all;}}
      h3{{margin:4px 0;font-size:19px;}}
      h3 a{{color:#1a0dab;text-decoration:underline;}}
      .snippet{{color:#4d5156;font-size:14px;line-height:1.5;margin-top:4px;}}
      .footer{{margin-top:30px;padding-top:16px;border-top:1px solid #e4e4e4;font-size:13px;color:#70757a;}}
      .footer a{{color:#70757a;margin-right:14px;}}
    </style></head><body>
      <div class="hdr">
        <a class="logo" href="https://www.google.com">Google</a>
        <form action="https://www.google.com/search" method="GET">
          <input name="q" value="{q_esc}">
          <button>Search</button>
        </form>
      </div>
      <div class="tabs">
        <a class="act" href="#">All</a>
        <a href="#">Images</a>
        <a href="#">Videos</a>
        <a href="#">News</a>
        <a href="https://www.wikipedia.org">Wikipedia</a>
      </div>
      <p style="color:#70757a;font-size:13px;margin-bottom:20px;">About {len(results)*125000:,} results</p>
      {items_html}
      <div class="footer">
        <p>Google Search on FirstOS · <a href="https://www.google.com">Google Home</a> · <a href="about:home">Browser Home</a></p>
      </div>
    </body></html>"""
    return page.encode("utf-8")


def _fetch_youtube_feed(query: str) -> list[dict[str, str]]:
    target_q = query if query else "trending popular music videos"
    url = f"https://www.youtube.com/results?search_query={urllib.parse.quote_plus(target_q)}"
    headers = {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        "Accept-Language": "en-US,en;q=0.9",
        "Cookie": "SOCS=CAISHAgBEhJnd3NfMjAyMzA4MTAtMF9SQzIaAmVuIAEaBgiAo_CmBg; CONSENT=YES+cb"
    }
    vids = []
    try:
        req = urllib.request.Request(url, headers=headers)
        with urllib.request.urlopen(req, timeout=12, context=unverified_context) as r:
            html_text = r.read().decode("utf-8", "replace")
        m = re.search(r"var ytInitialData\s*=\s*(\{.*?\});</script>", html_text, re.S)
        if m:
            data = json.loads(m.group(1))
            def find_vids(obj):
                if isinstance(obj, dict):
                    if "videoRenderer" in obj and isinstance(obj["videoRenderer"], dict):
                        vr = obj["videoRenderer"]
                        vid = vr.get("videoId", "")
                        title = ""
                        if "title" in vr and "runs" in vr["title"] and vr["title"]["runs"]:
                            title = vr["title"]["runs"][0].get("text", "")
                        elif "title" in vr and "simpleText" in vr["title"]:
                            title = vr["title"]["simpleText"]
                        channel = ""
                        if "ownerText" in vr and "runs" in vr["ownerText"] and vr["ownerText"]["runs"]:
                            channel = vr["ownerText"]["runs"][0].get("text", "")
                        elif "longBylineText" in vr and "runs" in vr["longBylineText"] and vr["longBylineText"]["runs"]:
                            channel = vr["longBylineText"]["runs"][0].get("text", "")
                        views = vr.get("shortViewCountText", {}).get("simpleText", "") or vr.get("viewCountText", {}).get("simpleText", "")
                        length = vr.get("lengthText", {}).get("simpleText", "")
                        if vid and title:
                            vids.append({"id": vid, "title": title, "channel": channel or "YouTube Creator", "views": views or "1.2M", "duration": length or "3:40"})
                    else:
                        for v in obj.values(): find_vids(v)
                elif isinstance(obj, list):
                    for it in obj: find_vids(it)
            find_vids(data)
    except Exception:
        pass

    if not vids:
        # High-quality fallback YouTube videos if network extraction fails
        vids = [
            {"id": "UZWmtxLiiFE", "title": "Nat King Cole - L-O-V-E (Official)", "channel": "Nat King Cole", "views": "9.7M", "duration": "2:35"},
            {"id": "jcbEiZQ9B7o", "title": "our conception of love is messed up.", "channel": "oliSUNvia", "views": "7.8M", "duration": "25:46"},
            {"id": "Hm2kgIdnSI4", "title": "love without attachment", "channel": "Sisyphus 55", "views": "2.7M", "duration": "5:36"},
            {"id": "tmSzRx9RYLk", "title": "Frank Sinatra - L.O.V.E. (lyrics)", "channel": "lIBioHazardIl", "views": "61M", "duration": "2:32"},
            {"id": "TBM64bgVMP4", "title": "why love is f*cked for gen z", "channel": "Lindsiann", "views": "1M", "duration": "13:06"},
            {"id": "f_HmF84G7ZY", "title": "Nat King Cole - L-O-V-E (Lyric Video)", "channel": "NatKingColeVEVO", "views": "45M", "duration": "2:36"}
        ]
    return vids


def _render_youtube_feed_html(query: str, videos: list[dict[str, str]]) -> bytes:
    q_esc = html.escape(query)
    cards_html = ""
    for v in videos:
        vid = html.escape(v.get("id", ""))
        title = html.escape(v.get("title", ""))
        channel = html.escape(v.get("channel", "YouTube Creator"))
        views = html.escape(v.get("views", "100K"))
        dur = html.escape(v.get("duration", "3:45"))
        thumb = f"https://i.ytimg.com/vi/{vid}/mqdefault.jpg"
        cards_html += f"""
        <div class="vcard">
          <div class="thumb-wrap">
            <a href="https://www.youtube.com/watch?v={vid}">
              <img src="{thumb}" width="240" height="135">
              <span class="dur">{dur}</span>
            </a>
          </div>
          <div class="info">
            <h3><a href="https://www.youtube.com/watch?v={vid}">{title}</a></h3>
            <p class="meta">{channel}</p>
            <p class="meta">{views} views</p>
          </div>
        </div>"""

    page = f"""<!doctype html><html><head><title>{(q_esc + " - ") if q_esc else ""}YouTube</title>
    <style>
      body{{font-family:sans-serif;margin:0;padding:16px 24px;background:#0F0F0F;color:#F1F1F1;}}
      .hdr{{display:flex;align-items:center;margin-bottom:16px;}}
      .logo{{color:#FF0000;font-size:24px;font-weight:bold;margin-right:24px;text-decoration:none;}}
      input{{width:420px;height:32px;background:#121212;border:1px solid #303030;color:#fff;border-radius:16px 0 0 16px;padding:4px 14px;font-size:15px;}}
      button{{background:#222;color:#fff;border:1px solid #303030;border-left:none;border-radius:0 16px 16px 0;padding:6px 18px;font-weight:bold;cursor:pointer;}}
      .pills{{display:flex;gap:10px;margin-bottom:20px;}}
      .pill{{background:#272727;color:#fff;padding:6px 14px;border-radius:8px;text-decoration:none;font-size:14px;margin-right:8px;}}
      .pill.act{{background:#fff;color:#000;font-weight:bold;}}
      .grid{{display:grid;grid-template-columns:repeat(3,1fr);gap:18px;}}
      .vcard{{background:#181818;border-radius:8px;overflow:hidden;padding:10px;}}
      .thumb-wrap{{position:relative;margin-bottom:6px;}}
      .thumb-wrap img{{border-radius:6px;width:100%;height:auto;display:block;}}
      .dur{{position:absolute;bottom:6px;right:6px;background:rgba(0,0,0,0.85);color:#fff;padding:2px 6px;font-size:12px;border-radius:4px;font-weight:bold;}}
      .info h3{{font-size:15px;margin:8px 0 4px;line-height:1.3;}}
      .info h3 a{{color:#F1F1F1;text-decoration:none;}}
      .meta{{font-size:13px;color:#AAAAAA;margin:3px 0;}}
    </style></head><body>
      <div class="hdr">
        <a class="logo" href="https://www.youtube.com">YouTube</a>
        <form action="https://www.youtube.com/results" method="GET">
          <input name="search_query" value="{q_esc}" placeholder="Search">
          <button>Search</button>
        </form>
      </div>
      <div class="pills">
        <a class="pill act" href="https://www.youtube.com">All</a>
        <a class="pill" href="https://www.youtube.com/results?search_query=Music">Music</a>
        <a class="pill" href="https://www.youtube.com/results?search_query=Gaming">Gaming</a>
        <a class="pill" href="https://www.youtube.com/results?search_query=News">News</a>
        <a class="pill" href="https://www.youtube.com/results?search_query=Science">Science</a>
      </div>
      <div class="grid">{cards_html}</div>
    </body></html>"""
    return page.encode("utf-8")


def _render_youtube_watch_html(video_id: str) -> bytes:
    vid_esc = html.escape(video_id)
    # Fetch title from YouTube
    title = f"YouTube Video ({vid_esc})"
    channel = "YouTube Creator"
    views = "2.4M"
    try:
        url = f"https://www.youtube.com/watch?v={vid_esc}"
        headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
            "Cookie": "SOCS=CAISHAgBEhJnd3NfMjAyMzA4MTAtMF9SQzIaAmVuIAEaBgiAo_CmBg; CONSENT=YES+cb"
        }
        req = urllib.request.Request(url, headers=headers)
        with urllib.request.urlopen(req, timeout=10, context=unverified_context) as r:
            raw = r.read().decode("utf-8", "replace")
        mt = re.search(r"<title>(.*?)</title>", raw)
        if mt:
            title = mt.group(1).replace(" - YouTube", "")
    except Exception:
        pass

    title_esc = html.escape(title)
    thumb = f"https://i.ytimg.com/vi/{vid_esc}/hqdefault.jpg"

    page = f"""<!doctype html><html><head><title>{title_esc} - YouTube</title>
    <style>
      body{{font-family:sans-serif;margin:0;padding:16px 24px;background:#0F0F0F;color:#F1F1F1;}}
      .hdr{{display:flex;align-items:center;margin-bottom:16px;}}
      .logo{{color:#FF0000;font-size:24px;font-weight:bold;margin-right:24px;text-decoration:none;}}
      input{{width:420px;height:32px;background:#121212;border:1px solid #303030;color:#fff;border-radius:16px 0 0 16px;padding:4px 14px;}}
      button{{background:#222;color:#fff;border:1px solid #303030;border-left:none;border-radius:0 16px 16px 0;padding:6px 18px;font-weight:bold;cursor:pointer;}}
      .player-wrap{{background:#000;border-radius:8px;overflow:hidden;margin-bottom:14px;}}
      h1{{font-size:20px;margin:12px 0 6px;}}
      .chan{{font-size:14px;color:#AAAAAA;margin-bottom:14px;}}
      .actions{{display:flex;gap:12px;margin-bottom:20px;}}
      .btn{{background:#272727;color:#fff;padding:8px 16px;border-radius:18px;text-decoration:none;font-weight:bold;font-size:13px;}}
      .back{{color:#3EA6FF;text-decoration:none;font-weight:bold;}}
    </style></head><body>
      <div class="hdr">
        <a class="logo" href="https://www.youtube.com">YouTube</a>
        <form action="https://www.youtube.com/results" method="GET">
          <input name="search_query" placeholder="Search">
          <button>Search</button>
        </form>
      </div>
      <div class="player-wrap">
        <video src="file:///video.mp4" poster="{thumb}" width="640" height="360" controls></video>
      </div>
      <h1>{title_esc}</h1>
      <p class="chan">{channel} · {views} views</p>
      <div class="actions">
        <span class="btn">Like</span>
        <span class="btn">Share</span>
        <span class="btn">Subscribe</span>
        <a class="btn back" href="https://www.youtube.com">Back to YouTube</a>
      </div>
    </body></html>"""
    return page.encode("utf-8")


class FirstOSProxyHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def do_GET(self) -> None:
        self._forward(False)

    def do_HEAD(self) -> None:
        self._forward(False, head_only=True)

    def do_POST(self) -> None:
        self._forward(True)

    def _send_error(self, status: int, message: str) -> None:
        payload = (message + "\n").encode("utf-8", "replace")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)

    def _forward(self, has_body: bool, head_only: bool = False) -> None:
        target = self.headers.get("X-FirstOS-Target", "").strip()
        if not target or len(target) > MAX_TARGET:
            self._send_error(400, "Missing or oversized X-FirstOS-Target")
            return

        parsed = urllib.parse.urlsplit(target)
        if parsed.scheme not in ("http", "https") or not parsed.hostname:
            self._send_error(400, "Unsupported target URL")
            return

        body = None
        if has_body:
            try:
                length = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                self._send_error(400, "Invalid request length")
                return
            if length < 0 or length > MAX_REQUEST_BODY:
                self._send_error(413, "Request body is too large")
                return
            body = self.rfile.read(length)

        # Detect resource type from URL and file extension for standards-compliant headers
        target_lower = target.lower()
        is_image = any(target_lower.endswith(ext) or (ext + "?") in target_lower for ext in (".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg", ".ico"))
        is_script = any(target_lower.endswith(ext) or (ext + "?") in target_lower for ext in (".js", ".mjs"))
        is_style = any(target_lower.endswith(ext) or (ext + "?") in target_lower for ext in (".css",))
        is_font = any(target_lower.endswith(ext) or (ext + "?") in target_lower for ext in (".woff", ".woff2", ".ttf", ".otf"))

        # Serve static assets from in-memory cache if available
        if not has_body and not head_only and (is_image or is_script or is_style or is_font):
            with _CACHE_LOCK:
                cached = _STATIC_CACHE.get(target)
            if cached:
                c_status, c_headers, c_body = cached
                self.send_response(c_status)
                for key in ("Content-Type", "Location", "Set-Cookie", "Cache-Control"):
                    v = c_headers.get(key)
                    if v:
                        self.send_header(key, v)
                self.send_header("Content-Length", str(len(c_body)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(c_body)
                self.log_message("%s %s -> %s (%d bytes) [CACHE HIT]", self.command, target, c_status, len(c_body))
                return

        if is_image:
            dest = "image"
            mode = "no-cors"
            accept = "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8"
        elif is_script:
            dest = "script"
            mode = "no-cors"
            accept = "*/*"
        elif is_style:
            dest = "style"
            mode = "no-cors"
            accept = "text/css,*/*;q=0.1"
        elif is_font:
            dest = "font"
            mode = "cors"
            accept = "*/*"
        else:
            dest = "document"
            mode = "navigate"
            accept = self.headers.get(
                "Accept",
                "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
            )


        headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
            "Accept": accept,
            "Accept-Language": "en-US,en;q=0.9",
            "Accept-Encoding": "identity",
            "Sec-Fetch-Dest": dest,
            "Sec-Fetch-Mode": mode,
            "Sec-Fetch-Site": "none" if dest == "document" else "cross-site",
        }
        if dest == "document":
            headers["Upgrade-Insecure-Requests"] = "1"
            headers["Sec-Fetch-User"] = "?1"

        for key, value in self.headers.items():
            lower = key.lower()
            if lower in {
                "host",
                "connection",
                "content-length",
                "accept-encoding",
                "x-firstos-target",
            }:
                continue
            if lower in {"content-type", "cookie", "referer"}:
                headers[key] = value

        request = urllib.request.Request(
            target,
            data=body,
            headers=headers,
            method="POST" if has_body else ("HEAD" if head_only else "GET"),
        )

        status = 502
        response_headers = {}
        response_body = b""

        for attempt in range(2):
            try:
                try:
                    opener = urllib.request.urlopen(request, timeout=30, context=ssl_context)
                except Exception:
                    opener = urllib.request.urlopen(request, timeout=30, context=unverified_context)
                with opener as response:
                    status = response.status
                    response_headers = dict(response.headers.items())
                    if not head_only:
                        response_body = response.read(MAX_RESPONSE_BODY + 1)
                break
            except urllib.error.HTTPError as error:
                status = error.code
                response_headers = dict(error.headers.items()) if error.headers else {}
                if not head_only:
                    response_body = error.read(MAX_RESPONSE_BODY + 1)
                if status in (429, 503) and attempt == 0:
                    time.sleep(0.3)
                    continue
                break
            except (urllib.error.URLError, TimeoutError, OSError) as error:
                if attempt == 0:
                    time.sleep(0.2)
                    continue
                self._send_error(502, "Host fetch failed: " + str(error))
                return

        if len(response_body) > MAX_RESPONSE_BODY:
            self._send_error(502, "Response body is too large")
            return

        # Store static assets in cache on successful 200 GET
        if status == 200 and not head_only and (is_image or is_script or is_style or is_font) and len(response_body) <= 2 * 1024 * 1024:
            with _CACHE_LOCK:
                if len(_STATIC_CACHE) > 500:
                    _STATIC_CACHE.clear()
                _STATIC_CACHE[target] = (status, response_headers, response_body)

        if dest == "document" and response_body:
            try:
                dec = response_body.decode("utf-8", "replace")
                dec = re.sub(r"<noscript\b[^>]*>.*?</noscript>", "", dec, flags=re.S|re.I)
                response_body = dec.encode("utf-8")
            except Exception:
                pass

        self.send_response(status)
        for key in ("Content-Type", "Location", "Set-Cookie", "Cache-Control"):
            value = response_headers.get(key)
            if value:
                self.send_header(key, value)
        self.send_header("Content-Length", str(0 if head_only else len(response_body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if not head_only:
            try:
                self.wfile.write(response_body)
            except Exception:
                pass

        self.log_message("%s %s -> %s (%d bytes)", self.command, target, status, len(response_body))

    def log_message(self, fmt: str, *args: object) -> None:
        sys.stderr.write("[firstos-webproxy] " + (fmt % args) + "\n")
        sys.stderr.flush()


class ReusableThreadingHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True


def main() -> int:
    parser = argparse.ArgumentParser(description="FirstOS QEMU HTTPS bridge")
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    args = parser.parse_args()

    server = ReusableThreadingHTTPServer((args.bind, args.port), FirstOSProxyHandler)
    print(f"[firstos-webproxy] listening on {args.bind}:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
