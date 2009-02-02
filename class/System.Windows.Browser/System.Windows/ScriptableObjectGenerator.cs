//
// System.Windows.ScriptableObjectGenerator class
//
// Contact:
//   Moonlight List (moonlight-list@lists.ximian.com)
//
// Copyright (C) 2007 Novell, Inc (http://www.novell.com)
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

using System;
using System.Collections.Generic;
using System.Text;
using System.Windows.Browser;
using System.Runtime.InteropServices;
using System.Reflection;
using System.Globalization;
using Mono;

namespace System.Windows
{
	// XXX This class shouldn't be needed.  MS just calls
	// Convert.ChangeType on the arguments, and *somehow* gets the
	// exception returned to JS.  We don't do that yet, so
	// unhandled exceptions crash the browser.  In an effort to
	// keep things limping along, we use this binder.
	class JSFriendlyMethodBinder : Binder {
		public override FieldInfo BindToField (BindingFlags bindingAttr, FieldInfo [] match, object value, CultureInfo culture)
		{
			throw new NotImplementedException ();
		}

		public override MethodBase BindToMethod (BindingFlags bindingAttr, MethodBase [] match, ref object [] args, ParameterModifier [] modifiers, CultureInfo culture, string [] names, out object state)
		{
			throw new NotImplementedException ();
		}

		public override object ChangeType (object value, Type type, CultureInfo culture)
		{
			if (value == null)
				return null;

			if (value.GetType() == type)
				return value;

			if (typeof(Enum).IsAssignableFrom (type.BaseType)) {
				try {
					return Enum.Parse (type, value.ToString(), true);
				} catch {
					throw new NotSupportedException ();
				}
			}

			/* the set of source types for JS functions is
			 * very, very small, so we switch over the
			 * parameter type first */
			try {
				return Convert.ChangeType (value, type, culture);
			}
			catch {
				// no clue if this is right.. if we
				// fail to convert, what do we return?

				switch (Type.GetTypeCode (type))
				{
				case TypeCode.Char:
				case TypeCode.Byte:
				case TypeCode.SByte:
				case TypeCode.Int16:
				case TypeCode.Int32:
				case TypeCode.Int64:
				case TypeCode.UInt16:
				case TypeCode.UInt32:
				case TypeCode.UInt64:
				case TypeCode.Single:
				case TypeCode.Double:
					return Convert.ChangeType (0, type, culture);

				case TypeCode.String:
					return "";

				case TypeCode.Boolean:
					return false;
				}
			}

			throw new NotSupportedException ();
		}

		public override void ReorderArgumentArray (ref object [] args, object state)
		{
			throw new NotImplementedException ();
		}

		public override MethodBase SelectMethod (BindingFlags bindingAttr, MethodBase [] match, Type [] types, ParameterModifier [] modifiers)
		{
			throw new NotImplementedException ();
		}

		public override PropertyInfo SelectProperty (BindingFlags bindingAttr, PropertyInfo [] match, Type returnType, Type [] indexes, ParameterModifier [] modifiers)
		{
			throw new NotImplementedException ();
		}
	}


	internal static class ScriptableObjectGenerator
	{
		static bool ValidateType (Type t)
		{
			if (!t.IsDefined (typeof(ScriptableTypeAttribute), true)) {
				if (ValidateProperties (t) | ValidateMethods (t) | ValidateEvents (t))
					return true;
			} else
				return true;
			return false;
		}

		static bool ValidateProperties (Type t) {
			bool ret = false;

			foreach (PropertyInfo pi in t.GetProperties ()) {
				if (!pi.IsDefined (typeof(ScriptableMemberAttribute), true))
					continue;

				if (!IsSupportedType (pi.PropertyType)) {
					throw new NotSupportedException (
						 String.Format ("The scriptable object type {0} has a property {1} whose type {2} is not supported",
								t, pi, pi.PropertyType));
				}
				ret = true;
			}
			return ret;
		}

		static bool ValidateMethods (Type t) {
			bool ret = false;

			foreach (MethodInfo mi in t.GetMethods ()) {
				if (!mi.IsDefined (typeof(ScriptableMemberAttribute), true))
					continue;

				if (mi.ReturnType != typeof (void) && !IsSupportedType (mi.ReturnType))
					throw new NotSupportedException (
						 String.Format ("The scriptable object type {0} has a method {1} whose return type {2} is not supported",
								t, mi, mi.ReturnType));

				ParameterInfo[] ps = mi.GetParameters();
				foreach (ParameterInfo p in ps) {
					if (p.IsOut || !IsSupportedType (p.ParameterType))
						throw new NotSupportedException (
						 String.Format ("The scriptable object type {0} has a method {1} whose parameter {2} is of not supported type",
								t, mi, p));
				}

				ret = true;
			}

			return ret;
		}

		static bool ValidateEvents (Type t) {
			bool ret = false;

			foreach (EventInfo ei in t.GetEvents ()) {
				if (!ei.IsDefined (typeof(ScriptableMemberAttribute), true))
					continue;

// 				Console.WriteLine ("event handler type = {0}", ei.EventHandlerType);
// 				Console.WriteLine ("typeof (EventHandler<>) == {0}", typeof (EventHandler<>));

				if (ei.EventHandlerType != typeof (EventHandler) && 
				    typeof (EventHandler<>).IsAssignableFrom (ei.EventHandlerType)) {
					if (!ValidateType (ei.EventHandlerType)) {
						throw new NotSupportedException (
							String.Format ("The scriptable object type {0} has a event {1} whose type {2} is not supported",
							t, ei, ei.EventHandlerType));
					}
				}

				ret = true;
			}
			return ret;
		}

		public static ScriptableObjectWrapper Generate (object instance, bool validate)
		{
			Type type = instance.GetType ();

			if (validate && !ValidateType (type))
				throw new ArgumentException (String.Format ("The scriptable type {0} does not have scriptable members", type));

			ScriptableObjectWrapper scriptable = new ScriptableObjectWrapper (instance);

			bool isScriptable = type.IsDefined (typeof(ScriptableTypeAttribute), true);

			// add properties

			foreach (PropertyInfo pi in type.GetProperties ()) {
				if (!isScriptable && !pi.IsDefined (typeof(ScriptableMemberAttribute), true))
					continue;
				scriptable.AddProperty (pi);
			}

			// add events
			foreach (EventInfo ei in type.GetEvents ()) {
				if (!isScriptable && !ei.IsDefined (typeof(ScriptableMemberAttribute), true))
					continue;
				scriptable.AddEvent (ei);
			}

			// add functions
			foreach (MethodInfo mi in type.GetMethods ()) {
				if (!isScriptable && !mi.IsDefined (typeof(ScriptableMemberAttribute), true))
					continue;
				scriptable.AddMethod (mi);
				AddTypes (mi);
			}
			return scriptable;
		}

		static void AddTypes (PropertyInfo pi)
		{
			if (IsCreateable (pi.PropertyType))
				AddType (pi.PropertyType);
		}

		static void AddTypes (MethodInfo mi)
		{
			if (IsCreateable (mi.ReturnType))
				AddType (mi.ReturnType);

			ParameterInfo[] ps = mi.GetParameters();
			foreach (ParameterInfo p in ps)
				AddType (p.ParameterType);
		}

		static void AddType (Type type)
		{
			if (!WebApplication.ScriptableTypes.ContainsKey (type.Name))
				WebApplication.ScriptableTypes[type.Name] = type;
		}

		static bool IsSupportedType (Type t)
		{
			TypeCode tc = Type.GetTypeCode (t);
			if (tc == TypeCode.Object) {
				return ValidateType (t);
			}

			switch (tc) {
			// string
			case TypeCode.Char:
			case TypeCode.String:
			// boolean
			case TypeCode.Boolean:
			// number
			case TypeCode.Byte:
			case TypeCode.SByte:
			case TypeCode.Int16:
			case TypeCode.Int32:
			case TypeCode.Int64:
			case TypeCode.UInt16:
			case TypeCode.UInt32:
			case TypeCode.UInt64:
			case TypeCode.Single:
			case TypeCode.Double:
			// case TypeCode.Decimal: // decimal is unsupported(!)
				return true;
			}

			return false;
		}

		static bool IsCreateable (Type type)
		{
			if (type != null && type != typeof (object))
				return false;

			if (!type.IsVisible || type.IsAbstract ||
			    type.IsInterface || type.IsPrimitive ||
				type.IsGenericTypeDefinition)
				return false;

			if (!type.IsValueType)
				return false;

			// default constructor
			if (type.GetConstructor (BindingFlags.Public | BindingFlags.Instance, null, new Type[0], null) == null)
				return false;

			return true;
		}
	}
}
