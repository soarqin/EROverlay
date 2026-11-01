use crate::gfx::{Point, VectorShape};

#[derive(Clone, Default, Debug)]
pub struct PaintMesh {
    pub vertices: Vec<Point>,
    pub indices: Vec<u32>,
    pub color: [u8; 4],
}

fn cross(a: Point, b: Point, c: Point) -> f32 {
    (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
}

fn triangulate(mut points: Vec<Point>, color: [u8; 4]) -> Option<PaintMesh> {
    if points.len() > 1 && points.first() == points.last() {
        points.pop();
    }
    points.dedup();
    if points.len() < 3 || points.len() > 8192 || points.iter().flatten().any(|v| !v.is_finite()) {
        return None;
    }
    let area: f32 = (0..points.len())
        .map(|i| {
            points[i][0] * points[(i + 1) % points.len()][1]
                - points[(i + 1) % points.len()][0] * points[i][1]
        })
        .sum();
    if area < 0.0 {
        points.reverse();
    }
    let mut remaining: Vec<usize> = (0..points.len()).collect();
    let mut indices = Vec::with_capacity((points.len() - 2) * 3);
    while remaining.len() > 3 {
        let mut ear = None;
        for i in 0..remaining.len() {
            let a = remaining[(i + remaining.len() - 1) % remaining.len()];
            let b = remaining[i];
            let c = remaining[(i + 1) % remaining.len()];
            if cross(points[a], points[b], points[c]) <= 1e-6 {
                continue;
            }
            let contains = remaining.iter().any(|&v| {
                v != a
                    && v != b
                    && v != c
                    && cross(points[a], points[b], points[v]) >= 0.0
                    && cross(points[b], points[c], points[v]) >= 0.0
                    && cross(points[c], points[a], points[v]) >= 0.0
            });
            if !contains {
                ear = Some((i, a, b, c));
                break;
            }
        }
        let (i, a, b, c) = ear?;
        indices.extend_from_slice(&[a as u32, b as u32, c as u32]);
        remaining.remove(i);
    }
    indices.extend(remaining.into_iter().map(|i| i as u32));
    Some(PaintMesh {
        vertices: points,
        indices,
        color,
    })
}

pub fn compile(shape: &VectorShape) -> Vec<PaintMesh> {
    let mut meshes = Vec::new();
    for (fill, style) in shape.fills.iter().enumerate() {
        let mut points = Vec::new();
        let mut previous = [0.0, 0.0];
        let finish = |points: &mut Vec<Point>, meshes: &mut Vec<PaintMesh>| {
            if let Some(mesh) = triangulate(std::mem::take(points), style.color) {
                meshes.push(mesh);
            }
        };
        for command in &shape.commands {
            if command.op == 0
                || (usize::from(command.fill0) != fill + 1
                    && usize::from(command.fill1) != fill + 1)
            {
                finish(&mut points, &mut meshes);
            } else {
                if points.is_empty() {
                    points.push(previous);
                }
                if command.op == 1 {
                    points.push(command.to);
                } else {
                    for step in 1..=12 {
                        let t = step as f32 / 12.0;
                        let u = 1.0 - t;
                        points.push([
                            u * u * previous[0]
                                + 2.0 * u * t * command.control[0]
                                + t * t * command.to[0],
                            u * u * previous[1]
                                + 2.0 * u * t * command.control[1]
                                + t * t * command.to[1],
                        ]);
                    }
                }
            }
            previous = command.to;
        }
        finish(&mut points, &mut meshes);
    }
    meshes
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn concave_fill_and_reversed_winding() {
        let polygon = vec![[0.0, 0.0], [2.0, 0.0], [1.0, 1.0], [2.0, 2.0], [0.0, 2.0]];
        let a = triangulate(polygon.clone(), [255; 4]).unwrap();
        let b = triangulate(polygon.into_iter().rev().collect(), [255; 4]).unwrap();
        assert_eq!(a.indices.len(), 9);
        assert_eq!(b.indices.len(), 9);
        assert!(triangulate(vec![[f32::NAN, 0.0]; 3], [0; 4]).is_none());
    }
}
