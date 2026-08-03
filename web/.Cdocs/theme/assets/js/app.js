// Cdocs 前端入口（引导文件）
// 生成器在 </body> 前注入 <script src="assets/js/app.js"></script>（classic script，不可改）。
// 这里仅用动态 import() 加载真正的 ES Module 模块图（同目录 main.js），
// 从而实现模块化，且无需重编译 C++ 生成器。
// 注意：import 路径相对本文件（assets/js/），main.js 与 app.js 同级。
import('./main.js')
  .catch(function (err) { console.error('[app] 模块加载失败：', err); });
