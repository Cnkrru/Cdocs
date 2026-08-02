# KaTeX 公式测试

本页验证数学公式渲染（客户端懒加载 KaTeX + auto-render）。行内公式用 `$...$`，块级公式用 `$$...$$`。

## 行内公式

- 质能方程：$E = mc^2$
- 欧拉恒等式：$e^{i\pi} + 1 = 0$
- 黄金比例：$\varphi = \frac{1 + \sqrt{5}}{2}$
- 组合数：$\binom{n}{k} = \frac{n!}{k!(n-k)!}$
- 行内积分：$\int_a^b f(x)\,dx$

## 块级公式

分部积分公式：

$$
\int u\,dv = uv - \int v\,du
$$

正态分布：

$$
f(x \mid \mu, \sigma^2) = \frac{1}{\sqrt{2\pi\sigma^2}} e^{-\frac{(x-\mu)^2}{2\sigma^2}}
$$

傅里叶级数：

$$
f(t) = \frac{a_0}{2} + \sum_{n=1}^{\infty} \left( a_n \cos\frac{2\pi nt}{T} + b_n \sin\frac{2\pi nt}{T} \right)
$$

## 矩阵

$$
\begin{pmatrix}
1 & 0 & 0 \\
0 & 1 & 0 \\
0 & 0 & 1
\end{pmatrix}
$$

## 分段函数

$$
|x| =
\begin{cases}
x,  & x \ge 0 \\
-x, & x < 0
\end{cases}
$$

## 希腊字母与运算符

$\alpha$ $\beta$ $\gamma$ $\delta$ $\epsilon$ $\lambda$ $\pi$ $\sigma$ $\omega$

$$
\lim_{x \to \infty} \frac{\sin x}{x} = 0, \qquad \prod_{k=1}^{n} k = n!
$$

## 混合示例（公式 + 文字）

当 $n \to \infty$ 时，$\left(1 + \frac{1}{n}\right)^n \to e$，这就是自然对数的定义之一。
