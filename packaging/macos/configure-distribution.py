"""Add the brand welcome page to a productbuild-generated distribution."""
import sys
from pathlib import Path
import xml.etree.ElementTree as ET
path = Path(sys.argv[1])
tree = ET.parse(path)
root = tree.getroot()
title = root.find("title")
if title is None:
    title = ET.SubElement(root, "title")
title.text = "Duomi Suno AI Bridge"
welcome = root.find("welcome")
if welcome is None:
    welcome = ET.SubElement(root, "welcome")
welcome.set("file", "welcome.html")
tree.write(path, encoding="utf-8", xml_declaration=True)
