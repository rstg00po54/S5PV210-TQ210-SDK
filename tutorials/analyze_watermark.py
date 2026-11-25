import fitz  # pip install PyMuPDF

input_pdf = "output.pdf"

def extract_text_from_block(block):
    text = ""
    if block["type"] == 0:  # 文本块
        for line in block.get("lines", []):
            for span in line.get("spans", []):
                text += span.get("text", "")
    return text.strip()

def analyze_pdf_watermark(input_pdf):
    doc = fitz.open(input_pdf)
    for page_number in range(len(doc)):
        page = doc[page_number]
        print(f"\n--- Page {page_number + 1} ---")

        # 1️⃣ 检查图片对象
        image_list = page.get_images(full=True)
        print(f"Images found: {len(image_list)}")
        for idx, img in enumerate(image_list):
            xref = img[0]
            info = doc.extract_image(xref)
            print(f"  Image {idx+1}: format={info['ext']}, size={info['width']}x{info['height']}")

        # 2️⃣ 检查文字块
        blocks = page.get_text("dict")["blocks"]
        text_blocks = []
        for b in blocks:
            t = extract_text_from_block(b)
            if t:
                text_blocks.append((t, b.get("bbox")))
        print(f"Text blocks found: {len(text_blocks)}")
        for idx, (t, bbox) in enumerate(text_blocks):
            print(f"  Text block {idx+1}: '{t}' | bbox={bbox}")

        # 3️⃣ 检查矢量/绘图对象
        drawings = page.get_drawings()
        print(f"Drawings found: {len(drawings)}")
        for idx, d in enumerate(drawings):
            bbox = d.get("bbox", None)
            fill = d.get("fill", None)
            color = d.get("color", None)
            print(f"  Drawing {idx+1}: type={d['type']}, bbox={bbox}, fill={fill}, color={color}")

if __name__ == "__main__":
    analyze_pdf_watermark(input_pdf)
