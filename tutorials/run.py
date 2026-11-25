import fitz  # pip install PyMuPDF

# 直接在代码里指定 PDF 路径
input_pdf = "天嵌科技出品-TQ210 开发板使用手册V1.7_20150820.pdf"      # 要处理的原始 PDF
output_pdf = "output.pdf"    # 去掉背景图片后的 PDF



def remove_specific_images(input_pdf, output_pdf, target_width, target_height):
    doc = fitz.open(input_pdf)
    for page_number in range(len(doc)):
        page = doc[page_number]
        image_list = page.get_images(full=True)
        print(f"Page {page_number + 1} has {len(image_list)} images")
        for img_index, img in enumerate(image_list):
            xref = img[0]
            base_image = doc.extract_image(xref)
            width = base_image["width"]
            height = base_image["height"]
            print(f"  Image {img_index + 1}: {width}x{height}")
            if width == target_width and height == target_height:
                try:
                    page.delete_image(xref)
                    print(f"    Deleted image {img_index + 1} ({width}x{height})")
                except Exception as e:
                    print(f"    Failed to delete image {img_index + 1}: {e}")
        print(f"Processed page {page_number + 1}/{len(doc)}")
    doc.save(output_pdf)
    print(f"Saved new PDF with 800x480 images removed: {output_pdf}")

# 只删除 800x480 的图片
remove_specific_images(input_pdf, output_pdf, 3464, 3984)