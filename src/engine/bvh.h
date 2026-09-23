// Jerarquia de cajas (BVH) generica: la usan la seleccion por rayo y el
// enganche, que tienen que responder en milisegundos sobre millones de
// triangulos o segmentos.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "geom.h"

namespace stp {

class Bvh {
public:
    // boxOf(i, &lo, &hi) da la caja de la primitiva i.
    template <typename BoxOf>
    void build(std::size_t count, BoxOf boxOf, std::uint32_t leafSize = 8) {
        m_nodes.clear();
        m_order.resize(count);
        if (count == 0) return;
        std::vector<float> centers(count * 3);
        m_lo.resize(count);
        m_hi.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            m_order[i] = static_cast<std::uint32_t>(i);
            boxOf(i, &m_lo[i], &m_hi[i]);
            centers[3 * i] = static_cast<float>((m_lo[i].x + m_hi[i].x) * 0.5);
            centers[3 * i + 1] = static_cast<float>((m_lo[i].y + m_hi[i].y) * 0.5);
            centers[3 * i + 2] = static_cast<float>((m_lo[i].z + m_hi[i].z) * 0.5);
        }
        m_nodes.reserve(2 * count / leafSize + 2);
        buildRange(centers, 0, static_cast<std::uint32_t>(count), leafSize);
        m_lo.clear();
        m_lo.shrink_to_fit();
        m_hi.clear();
        m_hi.shrink_to_fit();
    }

    bool empty() const { return m_nodes.empty(); }

    // enter(lo, hi) decide si se baja por un nodo; visit(i) recibe cada primitiva.
    template <typename Enter, typename Visit>
    void query(Enter enter, Visit visit) const {
        if (m_nodes.empty()) return;
        std::uint32_t stack[96];
        int top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const std::uint32_t index = stack[--top];
            const Node& node = m_nodes[index];
            if (!enter(node.lo, node.hi)) continue;
            if (node.count > 0) {
                for (std::uint32_t i = node.start; i < node.start + node.count; ++i) visit(m_order[i]);
            } else if (top + 2 <= 96) {
                stack[top++] = node.right;
                stack[top++] = index + 1;
            }
        }
    }

private:
    struct Node {
        Vec3 lo, hi;
        std::uint32_t start = 0, count = 0;  // hoja si count > 0
        std::uint32_t right = 0;             // el hijo izquierdo es index + 1
    };

    std::uint32_t buildRange(const std::vector<float>& centers, std::uint32_t start,
                             std::uint32_t end, std::uint32_t leafSize) {
        const std::uint32_t index = static_cast<std::uint32_t>(m_nodes.size());
        m_nodes.emplace_back();
        BBox bounds, middle;
        for (std::uint32_t i = start; i < end; ++i) {
            const std::uint32_t k = m_order[i];
            bounds.add(m_lo[k]);
            bounds.add(m_hi[k]);
            middle.add(Vec3(centers[3 * k], centers[3 * k + 1], centers[3 * k + 2]));
        }
        m_nodes[index].lo = bounds.lo;
        m_nodes[index].hi = bounds.hi;
        const Vec3 extent = middle.size();
        const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
        const double spread = axis == 0 ? extent.x : (axis == 1 ? extent.y : extent.z);
        if (end - start <= leafSize || spread <= 0) {
            m_nodes[index].start = start;
            m_nodes[index].count = end - start;
            return index;
        }
        const std::uint32_t mid = start + (end - start) / 2;
        std::nth_element(m_order.begin() + start, m_order.begin() + mid, m_order.begin() + end,
                         [&](std::uint32_t a, std::uint32_t b) {
                             return centers[3 * a + axis] < centers[3 * b + axis];
                         });
        buildRange(centers, start, mid, leafSize);
        const std::uint32_t right = buildRange(centers, mid, end, leafSize);
        m_nodes[index].right = right;
        return index;
    }

    std::vector<Node> m_nodes;
    std::vector<std::uint32_t> m_order;
    std::vector<Vec3> m_lo, m_hi;  // solo durante la construccion
};

}  // namespace stp
