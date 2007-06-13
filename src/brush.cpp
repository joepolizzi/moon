/*
 * brush.cpp: Brushes
 *
 * Author:
 *   Miguel de Icaza (miguel@novell.com)
 *
 * Copyright 2007 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 * 
 */
#define __STDC_CONSTANT_MACROS
#include <unistd.h>
#include <string.h>
#include <gtk/gtk.h>
#include <cairo.h>
#include <malloc.h>
#include <glib.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include "brush.h"
#include "transform.h"

//
// SL-Cairo convertion and helper routines
//

static cairo_extend_t
convert_gradient_spread_method (GradientSpreadMethod method)
{
	switch (method) {
	case GradientSpreadMethodPad:
		return CAIRO_EXTEND_PAD;
	case GradientSpreadMethodReflect:
		return CAIRO_EXTEND_REFLECT;
	case GradientSpreadMethodRepeat:
		return CAIRO_EXTEND_REPEAT;
	}
	g_warning ("Invalid GradientSpreadMethod value");
	return CAIRO_EXTEND_NONE;
}

//
// Brush
//

DependencyProperty* Brush::OpacityProperty;
DependencyProperty* Brush::RelativeTransformProperty;
DependencyProperty* Brush::TransformProperty;

double
brush_get_opacity (Brush *brush)
{
	return brush->GetValue (Brush::OpacityProperty)->AsDouble();
}

void
brush_set_opacity (Brush *brush, double opacity)
{
	brush->SetValue (Brush::OpacityProperty, Value (opacity));
}

TransformGroup*
brush_get_relative_transform (Brush *brush)
{
	Value *value = brush->GetValue (Brush::RelativeTransformProperty);
	return value ? value->AsTransformGroup() : NULL;
}

void
brush_set_relative_transform (Brush *brush, TransformGroup* transform_group)
{
	brush->SetValue (Brush::RelativeTransformProperty, Value (transform_group));
}

TransformGroup*
brush_get_transform (Brush *brush)
{
	Value *value = brush->GetValue (Brush::TransformProperty);
	return value ? value->AsTransformGroup() : NULL;
}

void
brush_set_transform (Brush *brush, TransformGroup* transform_group)
{
	brush->SetValue (Brush::TransformProperty, Value (transform_group));
}

//
// SolidColorBrush
//

DependencyProperty* SolidColorBrush::ColorProperty;

void
SolidColorBrush::SetupBrush (cairo_t *target, UIElement *uielement)
{
	Color *color = solid_color_brush_get_color (this);

	double alpha = color->a;
	// apply UIElement opacity and Brush opacity on color's alpha
	if (uielement) {
		// this is recursive to parents
		while (uielement) {
			double uielement_opacity = uielement_get_opacity (uielement);
			if (uielement_opacity < 1.0)
				alpha *= uielement_opacity;

			// FIXME: we should be calling FrameworkElement::Parent
			uielement = uielement->parent;
		}
	}

	double brush_opacity = brush_get_opacity (this);
	if (brush_opacity < 1.0)
		alpha *= brush_opacity;
	
	cairo_set_source_rgba (target, color->r, color->g, color->b, alpha);
}

Color*
solid_color_brush_get_color (SolidColorBrush *solid_color_brush)
{
	return solid_color_brush->GetValue (SolidColorBrush::ColorProperty)->AsColor();
}

void
solid_color_brush_set_color (SolidColorBrush *solid_color_brush, Color *color)
{
	solid_color_brush->SetValue (SolidColorBrush::ColorProperty, Value (*color));
}

SolidColorBrush*
solid_color_brush_new ()
{
	return new SolidColorBrush ();
}

// match System.Windows.Media.Colors properties
typedef struct {
	const char *name;
	const unsigned int color;
} named_colors_t;

named_colors_t named_colors [] = {
	// NOTE: samples shows that XAML supports more than the colors defined in System.Windows.Media.Colors
	// in fact tests shows that all System.Drawing.Color seems to be available
	{ "transparent",		0x00FFFFFF },
	{ "aliceblue",			0xFFF0F8FF },
	{ "antiquewhite",		0xFFFAEBD7 },
	{ "aqua",			0xFF00FFFF },
	{ "aquamarine",			0xFF7FFFD4 },
	{ "azure",			0xFFF0FFFF },
	{ "beige",			0xFFF5F5DC },
	{ "bisque",			0xFFFFE4C4 },
	{ "black",			0xFF000000 },
	{ "blanchedalmond",		0xFFFFEBCD },
	{ "blue",			0xFF0000FF },
	{ "blueviolet",			0xFF8A2BE2 },
	{ "brown",			0xFFA52A2A },
	{ "burlywood",			0xFFDEB887 },
	{ "cadetblue",			0xFF5F9EA0 },
	{ "chartreuse",			0xFF7FFF00 },
	{ "chocolate",			0xFFD2691E },
	{ "coral",			0xFFFF7F50 },
	{ "cornflowerblue",		0xFF6495ED },
	{ "cornsilk",			0xFFFFF8DC },
	{ "crimson",			0xFFDC143C },
	{ "cyan",			0xFF00FFFF },
	{ "darkblue",			0xFF00008B },
	{ "darkcyan",			0xFF008B8B },
	{ "darkgoldenrod",		0xFFB8860B },
	{ "darkgray",			0xFFA9A9A9 },
	{ "darkgreen",			0xFF006400 },
	{ "darkkhaki",			0xFFBDB76B },
	{ "darkmagenta",		0xFF8B008B },
	{ "darkolivegreen",		0xFF556B2F },
	{ "darkorange",			0xFFFF8C00 },
	{ "darkorchid",			0xFF9932CC },
	{ "darkred",			0xFF8B0000 },
	{ "darksalmon",			0xFFE9967A },
	{ "darkseagreen",		0xFF8FBC8B },
	{ "darkslateblue",		0xFF483D8B },
	{ "darkslategray",		0xFF2F4F4F },
	{ "darkturquoise",		0xFF00CED1 },
	{ "darkviolet",			0xFF9400D3 },
	{ "deeppink",			0xFFFF1493 },
	{ "deepskyblue",		0xFF00BFFF },
	{ "dimgray",			0xFF696969 },
	{ "dodgerblue",			0xFF1E90FF },
	{ "firebrick",			0xFFB22222 },
	{ "floralwhite",		0xFFFFFAF0 },
	{ "forestgreen",		0xFF228B22 },
	{ "fuchsia",			0xFFFF00FF },
	{ "gainsboro",			0xFFDCDCDC },
	{ "ghostwhite",			0xFFF8F8FF },
	{ "gold",			0xFFFFD700 },
	{ "goldenrod",			0xFFDAA520 },
	{ "gray",			0xFF808080 },
	{ "green",			0xFF008000 },
	{ "greenyellow",		0xFFADFF2F },
	{ "honeydew",			0xFFF0FFF0 },
	{ "hotpink",			0xFFFF69B4 },
	{ "indianred",			0xFFCD5C5C },
	{ "indigo",			0xFF4B0082 },
	{ "ivory",			0xFFFFFFF0 },
	{ "khaki",			0xFFF0E68C },
	{ "lavender",			0xFFE6E6FA },
	{ "lavenderblush",		0xFFFFF0F5 },
	{ "lawngreen",			0xFF7CFC00 },
	{ "lemonchiffon",		0xFFFFFACD },
	{ "lightblue",			0xFFADD8E6 },
	{ "lightcoral",			0xFFF08080 },
	{ "lightcyan",			0xFFE0FFFF },
	{ "lightgoldenrodyellow",	0xFFFAFAD2 },
	{ "lightgreen",			0xFF90EE90 },
	{ "lightgray",			0xFFD3D3D3 },
	{ "lightpink",			0xFFFFB6C1 },
	{ "lightsalmon",		0xFFFFA07A },
	{ "lightseagreen",		0xFF20B2AA },
	{ "lightskyblue",		0xFF87CEFA },
	{ "lightslategray",		0xFF778899 },
	{ "lightsteelblue",		0xFFB0C4DE },
	{ "lightyellow",		0xFFFFFFE0 },
	{ "lime",			0xFF00FF00 },
	{ "limegreen",			0xFF32CD32 },
	{ "linen",			0xFFFAF0E6 },
	{ "magenta",			0xFFFF00FF },
	{ "maroon",			0xFF800000 },
	{ "mediumaquamarine",		0xFF66CDAA },
	{ "mediumblue",			0xFF0000CD },
	{ "mediumorchid",		0xFFBA55D3 },
	{ "mediumpurple",		0xFF9370DB },
	{ "mediumseagreen",		0xFF3CB371 },
	{ "mediumslateblue",		0xFF7B68EE },
	{ "mediumspringgreen",		0xFF00FA9A },
	{ "mediumturquoise",		0xFF48D1CC },
	{ "mediumvioletred",		0xFFC71585 },
	{ "midnightblue",		0xFF191970 },
	{ "mintcream",			0xFFF5FFFA },
	{ "mistyrose",			0xFFFFE4E1 },
	{ "moccasin",			0xFFFFE4B5 },
	{ "navajowhite",		0xFFFFDEAD },
	{ "navy",			0xFF000080 },
	{ "oldlace",			0xFFFDF5E6 },
	{ "olive",			0xFF808000 },
	{ "olivedrab",			0xFF6B8E23 },
	{ "orange",			0xFFFFA500 },
	{ "orangered",			0xFFFF4500 },
	{ "orchid",			0xFFDA70D6 },
	{ "palegoldenrod",		0xFFEEE8AA },
	{ "palegreen",			0xFF98FB98 },
	{ "paleturquoise",		0xFFAFEEEE },
	{ "palevioletred",		0xFFDB7093 },
	{ "papayawhip",			0xFFFFEFD5 },
	{ "peachpuff",			0xFFFFDAB9 },
	{ "peru",			0xFFCD853F },
	{ "pink",			0xFFFFC0CB },
	{ "plum",			0xFFDDA0DD },
	{ "powderblue",			0xFFB0E0E6 },
	{ "purple",			0xFF800080 },
	{ "red",			0xFFFF0000 },
	{ "rosybrown",			0xFFBC8F8F },
	{ "royalblue",			0xFF4169E1 },
	{ "saddlebrown",		0xFF8B4513 },
	{ "salmon",			0xFFFA8072 },
	{ "sandybrown",			0xFFF4A460 },
	{ "seagreen",			0xFF2E8B57 },
	{ "seashell",			0xFFFFF5EE },
	{ "sienna",			0xFFA0522D },
	{ "silver",			0xFFC0C0C0 },
	{ "skyblue",			0xFF87CEEB },
	{ "slateblue",			0xFF6A5ACD },
	{ "slategray",			0xFF708090 },
	{ "snow",			0xFFFFFAFA },
	{ "springgreen",		0xFF00FF7F },
	{ "steelblue",			0xFF4682B4 },
	{ "tan",			0xFFD2B48C },
	{ "teal",			0xFF008080 },
	{ "thistle",			0xFFD8BFD8 },
	{ "tomato",			0xFFFF6347 },
	{ "turquoise",			0xFF40E0D0 },
	{ "violet",			0xFFEE82EE },
	{ "wheat",			0xFFF5DEB3 },
	{ "white",			0xFFFFFFFF },
	{ "whitesmoke",			0xFFF5F5F5 },
	{ "yellow",			0xFFFFFF00 },
	{ "yellowgreen",		0xFF9ACD32 },
	{ NULL, 0 }
};

/**
 * see: http://msdn2.microsoft.com/en-us/library/system.windows.media.solidcolorbrush.aspx
 *
 * If no color is found, Color.Transparent is returned.
 */
Color*
color_from_str (const char *name)
{
	if (!name)
		return new Color (0x00FFFFFF);

	if (name [0] == '#') {
		char a [3] = "FF";
		char r [3] = "FF";
		char g [3] = "FF";
		char b [3] = "FF";

		switch (strlen (name + 1)) {
		case 3:
			// rgb
			r [1] = '0'; r [0] = name [1];
			g [1] = '0'; g [0] = name [2];
			b [1] = '0'; b [0] = name [3];
			break;
		case 4:
			// argb
			a [1] = '0'; a [0] = name [1];
			r [1] = '0'; r [0] = name [2];
			g [1] = '0'; g [0] = name [3];
			b [1] = '0'; b [0] = name [4];
			break;
		case 6:
			// rrggbb
			r [0] = name [1]; r [1] = name [2];
			g [0] = name [3]; g [1] = name [4];
			b [0] = name [5]; b [1] = name [6];
			break;
		case 8:
			// aarrggbb
			a [0] = name [1]; a [1] = name [2];
			r [0] = name [3]; r [1] = name [4];
			g [0] = name [5]; g [1] = name [6];
			b [0] = name [7]; b [1] = name [8];
			break;			
		}

		return new Color (strtol (r, NULL, 16) / 255.0F, strtol (g, NULL, 16) / 255.0F,
				strtol (b, NULL, 16) / 255.0F, strtol (a, NULL, 16) / 255.0F);
	}

	if (name [0] == 's' && name [1] == 'c' && name [2] == '#') {
		/* TODO */
	}

	for (int i = 0; named_colors [i].name; i++) {
		if (!g_strcasecmp (named_colors [i].name, name)) {
			return new Color (named_colors [i].color);
		}
	}

	return new Color (0x00FFFFFF);
}

//
// GradientBrush
//

DependencyProperty* GradientBrush::ColorInterpolationModeProperty;
DependencyProperty* GradientBrush::GradientStopsProperty;
DependencyProperty* GradientBrush::MappingModeProperty;
DependencyProperty* GradientBrush::SpreadMethodProperty;

ColorInterpolationMode
gradient_brush_get_color_interpolation_mode (GradientBrush *brush)
{
	return (ColorInterpolationMode) brush->GetValue (GradientBrush::ColorInterpolationModeProperty)->AsInt32();
}

void
gradient_brush_set_color_interpolation_mode (GradientBrush *brush, ColorInterpolationMode mode)
{
	brush->SetValue (GradientBrush::ColorInterpolationModeProperty, Value (mode));
}

GradientStopCollection*
gradient_brush_get_gradient_stops (GradientBrush *brush)
{
	Value *value = brush->GetValue (GradientBrush::GradientStopsProperty);
	return (GradientStopCollection*) (value ? value->AsGradientStopCollection() : NULL);
}

void
gradient_brush_set_gradient_stops (GradientBrush *brush, GradientStopCollection* collection)
{
	brush->SetValue (GradientBrush::GradientStopsProperty, Value (collection));
}

BrushMappingMode
gradient_brush_get_mapping_mode (GradientBrush *brush)
{
	return (BrushMappingMode) brush->GetValue (GradientBrush::MappingModeProperty)->AsInt32();
}

void
gradient_brush_set_mapping_mode (GradientBrush *brush, BrushMappingMode mode)
{
	brush->SetValue (GradientBrush::MappingModeProperty, Value (mode));
}

GradientSpreadMethod 
gradient_brush_get_spread (GradientBrush *brush)
{
	return (GradientSpreadMethod) brush->GetValue (GradientBrush::SpreadMethodProperty)->AsInt32();
}

void
gradient_brush_set_spread (GradientBrush *brush, GradientSpreadMethod method)
{
	brush->SetValue (GradientBrush::SpreadMethodProperty, Value (method));
}

GradientBrush::GradientBrush ()
{
	children = NULL;
	GradientStopCollection *c = new GradientStopCollection ();

	this->SetValue (GradientBrush::GradientStopsProperty, Value (c));

	// Ensure that the callback OnPropertyChanged was called.
	g_assert (c == children);
}

void
GradientBrush::OnPropertyChanged (DependencyProperty *prop)
{
	Brush::OnPropertyChanged (prop);

	if (prop == GradientStopsProperty){
		// The new value has already been set, so unref the old collection
		GradientStopCollection *newcol = GetValue (prop)->AsGradientStopCollection();

		if (newcol != children){
			if (children) {
				DependencyObject *dob;
				GSList *node, *next;
				
				node = children->list;
				while (node != NULL) {
					dob = (DependencyObject *) node->data;
					base_unref (dob);
					next = node->next;
					g_slist_free_1 (node);
					node = next;
				}
				
				base_unref (children);
			}

			children = newcol;
			if (children->closure)
				printf ("Warning we attached a property that was already attached\n");
			children->closure = this;
			
			base_ref (children);
		}
	}
}

void
GradientBrush::SetupPattern (cairo_pattern_t *pattern)
{
	GradientSpreadMethod gsm = gradient_brush_get_spread (this);
	cairo_pattern_set_extend (pattern, convert_gradient_spread_method (gsm));

	// TODO - BrushMappingMode is ignore (use a matrix)

	// TODO - ColorInterpolationModeProperty is ignored (map to ?)

	for (GSList *g = children->list; g != NULL; g = g->next) {
		GradientStop *stop = (GradientStop*) g->data;
		Color *color = gradient_stop_get_color (stop);
		double offset = gradient_stop_get_offset (stop);
		cairo_pattern_add_color_stop_rgba (pattern, offset, color->r, color->g, color->b, color->a);
	}
}

//
// LinearGradientBrush
//

DependencyProperty* LinearGradientBrush::EndPointProperty;
DependencyProperty* LinearGradientBrush::StartPointProperty;

LinearGradientBrush*
linear_gradient_brush_new ()
{
	return new LinearGradientBrush ();
}

Point*
linear_gradient_brush_get_end_point (LinearGradientBrush *brush)
{
	Value *value = brush->GetValue (LinearGradientBrush::EndPointProperty);
	return (value ? value->AsPoint() : NULL);
}

void
linear_gradient_brush_set_end_point (LinearGradientBrush *brush, Point *point)
{
	brush->SetValue (LinearGradientBrush::EndPointProperty, Value (*point));
}

Point*
linear_gradient_brush_get_start_point (LinearGradientBrush *brush)
{
	Value *value = brush->GetValue (LinearGradientBrush::StartPointProperty);
	return (value ? value->AsPoint() : NULL);
}

void
linear_gradient_brush_set_start_point (LinearGradientBrush *brush, Point *point)
{
	brush->SetValue (LinearGradientBrush::StartPointProperty, Value (*point));
}

void
LinearGradientBrush::SetupBrush (cairo_t *cairo, UIElement *uielement)
{
	double w = framework_element_get_width ((FrameworkElement*)uielement);
	double h = framework_element_get_height ((FrameworkElement*)uielement);

	Point *start = linear_gradient_brush_get_start_point (this);
	double x0 = start ? (start->x * w) : 0.0;
	double y0 = start ? (start->y * h) : 0.0;

	Point *end = linear_gradient_brush_get_end_point (this);
	double x1 = end ? (end->x * w) : w;
	double y1 = end ? (end->y * h) : h;

	cairo_pattern_t *pattern = cairo_pattern_create_linear (x0, y0, x1, y1);

	GradientBrush::SetupPattern (pattern);

	cairo_set_source (cairo, pattern);
	cairo_pattern_destroy (pattern);
}

//
// RadialGradientBrush
//

DependencyProperty* RadialGradientBrush::CenterProperty;
DependencyProperty* RadialGradientBrush::GradientOriginProperty;
DependencyProperty* RadialGradientBrush::RadiusXProperty;
DependencyProperty* RadialGradientBrush::RadiusYProperty;

RadialGradientBrush*
radial_gradient_brush_new ()
{
	return new RadialGradientBrush ();
}

Point*
radial_gradient_brush_get_center (RadialGradientBrush *brush)
{
	Value *value = brush->GetValue (RadialGradientBrush::CenterProperty);
	return (value ? value->AsPoint() : new Point (0.5, 0.5));
}

void
radial_gradient_brush_set_center (RadialGradientBrush *brush, Point *center)
{
	brush->SetValue (RadialGradientBrush::CenterProperty, Value (*center));
}

Point*
radial_gradient_brush_get_gradientorigin (RadialGradientBrush *brush)
{
	Value *value = brush->GetValue (RadialGradientBrush::GradientOriginProperty);
	return (value ? value->AsPoint() : new Point (0.5, 0.5));
}

void
radial_gradient_brush_set_gradientorigin (RadialGradientBrush *brush, Point *origin)
{
	brush->SetValue (RadialGradientBrush::GradientOriginProperty, Value (*origin));
}

double
radial_gradient_brush_get_radius_x (RadialGradientBrush *brush)
{
	return brush->GetValue (RadialGradientBrush::RadiusXProperty)->AsDouble();
}

void
radial_gradient_brush_set_radius_x (RadialGradientBrush *brush, double radiusX)
{
	brush->SetValue (RadialGradientBrush::RadiusXProperty, Value (radiusX));
}

double
radial_gradient_brush_get_radius_y (RadialGradientBrush *brush)
{
	return brush->GetValue (RadialGradientBrush::RadiusYProperty)->AsDouble();
}

void
radial_gradient_brush_set_radius_y (RadialGradientBrush *brush, double radiusY)
{
	brush->SetValue (RadialGradientBrush::RadiusYProperty, Value (radiusY));
}

void
RadialGradientBrush::SetupBrush (cairo_t *cairo, UIElement *uielement)
{
	Point *center = radial_gradient_brush_get_center (this);
	Point *origin = radial_gradient_brush_get_gradientorigin (this);
	double rx = radial_gradient_brush_get_radius_x (this);
	double ry = radial_gradient_brush_get_radius_y (this);

	// FIXME - not ok
	cairo_pattern_t *pattern = cairo_pattern_create_radial (center->x, center->y, rx, origin->x, origin->y, ry);

	GradientBrush::SetupPattern (pattern);

	cairo_set_source (cairo, pattern);
	cairo_pattern_destroy (pattern);
}

//
// GradientStopCollection
//

void
GradientStopCollection::Add (void *data)
{
	Value *value = (Value*) data;
	GradientStop *gradient_stop = value->AsGradientStop ();
	Collection::Add (gradient_stop);
}

void GradientStopCollection::Remove (void *data)
{
	Value *value = (Value*) data;
	GradientStop *gradient_stop = value->AsGradientStop ();
	Collection::Remove (gradient_stop);
}

GradientStopCollection*
gradient_stop_collection_new ()
{
	return new GradientStopCollection ();
}

//
// GradientStop
//

DependencyProperty* GradientStop::ColorProperty;
DependencyProperty* GradientStop::OffsetProperty;

GradientStop*
gradient_stop_new ()
{
	return new GradientStop ();
}

Color*
gradient_stop_get_color (GradientStop *stop)
{
	return stop->GetValue (GradientStop::ColorProperty)->AsColor();
}

void
gradient_stop_set_color (GradientStop *stop, Color *color)
{
	stop->SetValue (GradientStop::ColorProperty, Value (*color));
}

double
gradient_stop_get_offset (GradientStop *stop)
{
	return stop->GetValue (GradientStop::OffsetProperty)->AsDouble();
}

void
gradient_stop_set_offset (GradientStop *stop, double offset)
{
	stop->SetValue (GradientStop::OffsetProperty, Value (offset));
}

//
//
//

void
brush_init ()
{
	/* Brush fields */
	Brush::OpacityProperty = DependencyObject::Register (Value::BRUSH, "Opacity", new Value (1.0));
	Brush::RelativeTransformProperty = DependencyObject::Register (Value::BRUSH, "RelativeTransform", Value::TRANSFORMGROUP);
	Brush::TransformProperty = DependencyObject::Register (Value::BRUSH, "Transform", Value::TRANSFORMGROUP);

	/* SolidColorBrush fields */
	SolidColorBrush::ColorProperty = DependencyObject::Register (Value::SOLIDCOLORBRUSH, "Color", new Value (Color (0x00FFFFFF)));

	/* GradientBrush fields */
	GradientBrush::ColorInterpolationModeProperty = DependencyObject::Register (Value::GRADIENTBRUSH, "ColorInterpolationMode",  new Value (0));
	GradientBrush::GradientStopsProperty = DependencyObject::Register (Value::GRADIENTBRUSH, "GradientStops", Value::GRADIENTSTOP_COLLECTION);
	GradientBrush::MappingModeProperty = DependencyObject::Register (Value::GRADIENTBRUSH, "MappingMode",  new Value (0));
	GradientBrush::SpreadMethodProperty = DependencyObject::Register (Value::GRADIENTBRUSH, "SpreadMethod",  new Value (0));

	/* LinearGradientBrush fields */
	LinearGradientBrush::EndPointProperty = DependencyObject::Register (Value::LINEARGRADIENTBRUSH, "EndPoint", Value::POINT);
	LinearGradientBrush::StartPointProperty = DependencyObject::Register (Value::LINEARGRADIENTBRUSH, "StartPoint", Value::POINT);

	/* RadialGradientBrush fields */
	RadialGradientBrush::CenterProperty = DependencyObject::Register (Value::RADIALGRADIENTBRUSH, "Center", Value::POINT);
	RadialGradientBrush::GradientOriginProperty = DependencyObject::Register (Value::RADIALGRADIENTBRUSH, "GradientOrigin", Value::POINT);
	RadialGradientBrush::RadiusXProperty = DependencyObject::Register (Value::RADIALGRADIENTBRUSH, "RadiusX",  new Value (0.5));
	RadialGradientBrush::RadiusYProperty = DependencyObject::Register (Value::RADIALGRADIENTBRUSH, "RadiusY",  new Value (0.5));

	/* GradientStop */
	GradientStop::ColorProperty = DependencyObject::Register (Value::GRADIENTSTOP, "Color", new Value (Color (0x00FFFFFF)));
	GradientStop::OffsetProperty = DependencyObject::Register (Value::GRADIENTSTOP, "Offset", new Value (0.0));
}
