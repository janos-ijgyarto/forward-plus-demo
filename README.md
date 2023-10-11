# Forward+ Demo
D3D11 application that demonstrates [Forward+](https://www.3dgep.com/forward-plus/) light culling and shading techniques.

The app generates a random layout of point and spot lights, along with some 3D primitives to help observe the results. The lights also have debug rendering to show their position and range.

The main goal, besides getting it to work at all, was to see if a relatively efficient implementation can be achieved without advanced compute shader features (e.g atomics)

- Inspiration: https://themaister.net/blog/2020/01/10/clustered-shading-evolution-in-granite/
- Lighting system and shader code adapted from: https://github.com/Themaister/Granite

Huge thanks to [Themaister](https://github.com/Themaister) for the blog and the code which inspired this work.
