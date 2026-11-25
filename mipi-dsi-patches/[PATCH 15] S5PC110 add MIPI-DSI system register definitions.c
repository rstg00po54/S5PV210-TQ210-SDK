Signed-off-by: Inki Dae <inki.dae@samsung.com>
---
 arch/arm/mach-s5pv210/include/mach/regs-clock.h |    6 +++++-
 1 files changed, 5 insertions(+), 1 deletions(-)

diff --git a/arch/arm/mach-s5pv210/include/mach/regs-clock.h b/arch/arm/mach-s5pv210/include/mach/regs-clock.h
index ebaabe0..09680d9 100644
--- a/arch/arm/mach-s5pv210/include/mach/regs-clock.h
+++ b/arch/arm/mach-s5pv210/include/mach/regs-clock.h
@@ -146,6 +146,7 @@
 #define S5P_OM_STAT		S5P_CLKREG(0xE100)
 #define S5P_USB_PHY_CONTROL	S5P_CLKREG(0xE80C)
 #define S5P_DAC_CONTROL		S5P_CLKREG(0xE810)
+#define S5P_MIPI_DPHY_CONTROL		S5P_CLKREG(0xE814)
 
 #define S5P_INFORM0		S5P_CLKREG(0xF000)
 #define S5P_INFORM1		S5P_CLKREG(0xF004)
@@ -196,7 +197,10 @@
 #define S5P_OTHERS_USB_SIG_MASK		(1 << 16)
 
 /* MIPI */
-#define S5P_MIPI_DPHY_EN		(3)
+#define S5P_MIPI_DPHY_EN		(3 << 0)
+
+/* S5P_MIPI_PHY_CON0 */
+#define S5P_MIPI_M_RESETN		(1 << 1)
 
 /* S5P_DAC_CONTROL */
 #define S5P_DAC_ENABLE			(1)
-- 
1.7.0.4

--
To unsubscribe from this list: send the line "unsubscribe linux-fbdev" in
the body of a message to majordomo@vger.kernel.org
More majordomo info at  http://vger.kernel.org/majordomo-info.html