import { Fragment, type ReactNode } from "react";
import { materialmc } from "../api/client";

/**
 * Renders backend text that may contain the small HTML subset Qt dialogs used (b, i, br, p, a, lists, ...).
 * The markup is parsed and rebuilt as React elements from an allow-list; nothing is ever inserted as raw HTML
 * and links only open http(s) URLs in the system browser.
 */
const ALLOWED: Record<string, string> = {
  b: "strong",
  strong: "strong",
  i: "em",
  em: "em",
  u: "u",
  s: "s",
  p: "p",
  ul: "ul",
  ol: "ol",
  li: "li",
  code: "code",
  pre: "pre",
  h1: "strong",
  h2: "strong",
  h3: "strong",
  h4: "strong",
  blockquote: "blockquote",
  table: "table",
  tbody: "tbody",
  tr: "tr",
  td: "td",
  th: "th",
};

const looksLikeHtml = (text: string) => /<\/?[a-z][^>]*>|&[a-z#0-9]+;/i.test(text);

function safeUrl(href: string | null): string | null {
  if (!href) return null;
  try {
    const url = new URL(href);
    return url.protocol === "http:" || url.protocol === "https:" ? url.toString() : null;
  } catch {
    return null;
  }
}

function plain(text: string): ReactNode {
  const lines = text.split("\n");
  return lines.map((line, i) => (
    <Fragment key={i}>
      {line}
      {i < lines.length - 1 && <br />}
    </Fragment>
  ));
}

function convert(node: Node, key: number): ReactNode {
  if (node.nodeType === Node.TEXT_NODE) return node.textContent;
  if (node.nodeType !== Node.ELEMENT_NODE) return null;
  const el = node as Element;
  const tag = el.tagName.toLowerCase();
  const children = Array.from(el.childNodes, (child, i) => convert(child, i));
  if (tag === "br") return <br key={key} />;
  if (tag === "hr") return <hr key={key} />;
  if (tag === "a") {
    const url = safeUrl(el.getAttribute("href"));
    if (!url) return <Fragment key={key}>{children}</Fragment>;
    return (
      <a
        key={key}
        href={url}
        onClick={(e) => {
          e.preventDefault();
          void materialmc.system.openUrl(url);
        }}
      >
        {children}
      </a>
    );
  }
  if (tag === "img") return null;
  const mapped = ALLOWED[tag];
  if (!mapped) return <Fragment key={key}>{children}</Fragment>; // html, body, span, font, div, ... keep content only
  const Tag = mapped as "strong";
  return <Tag key={key}>{children}</Tag>;
}

export function RichText({ text, className }: { text: string; className?: string }) {
  if (!looksLikeHtml(text)) {
    return <div className={className}>{plain(text)}</div>;
  }
  const doc = new DOMParser().parseFromString(text.replace(/\n/g, "<br/>"), "text/html");
  return <div className={`rich-text${className ? ` ${className}` : ""}`}>{Array.from(doc.body.childNodes, (n, i) => convert(n, i))}</div>;
}
