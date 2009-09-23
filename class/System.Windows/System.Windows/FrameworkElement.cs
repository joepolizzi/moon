//
// FrameworkElement.cs
//
// Contact:
//   Moonlight List (moonlight-list@lists.ximian.com)
//
// Copyright 2008 Novell, Inc.
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

using Mono;
using System.Reflection;
using System.Collections.Generic;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Markup;
using System.Windows.Media;

namespace System.Windows {
	public abstract partial class FrameworkElement : UIElement {

		static UnmanagedEventHandler on_loaded = Events.SafeDispatcher (
			    (IntPtr target, IntPtr calldata, IntPtr closure) =>
			    	((FrameworkElement) NativeDependencyObjectHelper.FromIntPtr (closure)).InvokeLoaded (NativeDependencyObjectHelper.FromIntPtr (calldata) as RoutedEventArgs ?? new RoutedEventArgs (calldata, false)));

		static FrameworkElement ()
		{
			StyleProperty.Validate = delegate (DependencyObject target, DependencyProperty propety, object value) {
				Type styleType = ((Style)value).TargetType;
				if (!styleType.IsAssignableFrom (target.GetType ()))
					throw new System.Windows.Markup.XamlParseException (string.Format ("Target is of type {0} but the Style requires {1}", target.GetType ().Name, styleType.Name));
			};

			DataContextProperty.AddPropertyChangeCallback (DataContextChanged);
		}

		static void DataContextChanged (DependencyObject sender, DependencyPropertyChangedEventArgs args)
		{
			(sender as FrameworkElement).OnDataContextChanged (args.OldValue, args.NewValue);
		}

		void OnDataContextChanged (object oldValue, object newValue)
		{
			InvalidateLocalBindings ();
			InvalidateSubtreeBindings ();
		}

		void InvalidateSubtreeBindings ()
		{
			for (int c = 0; c < VisualTreeHelper.GetChildrenCount (this); c++) {
				FrameworkElement obj = VisualTreeHelper.GetChild (this, c) as FrameworkElement;
				if (obj == null)
					continue;
				obj.InvalidateLocalBindings ();
				obj.InvalidateSubtreeBindings ();
			}
		}

		bool invalidatingLocalBindings;

		void InvalidateLocalBindings ()
		{
			if (expressions.Count == 0)
				return;

			if (invalidatingLocalBindings)
				return;

			invalidatingLocalBindings = true;

			Dictionary<DependencyProperty, Expression> old = expressions;
			expressions = new Dictionary<DependencyProperty, Expression> ();
			foreach (var keypair in old) {
				if (keypair.Value is BindingExpressionBase) {
					BindingExpressionBase beb = (BindingExpressionBase)keypair.Value;
					beb.Invalidate ();
					SetValue (keypair.Key, beb);
				}
				else if (keypair.Value is TemplateBindingExpression) {
					// we don't invalidate
					// templatebinding
					// expressions, so just add it
					// back to the expressions
					// list.
					expressions.Add (keypair.Key, keypair.Value);
				}
			}

			invalidatingLocalBindings = false;
		}

		/* 
		 * XXX these are marked internal because making them private seems
		 * to cause the GC to collect them
		 */
		internal MeasureOverrideCallback measure_cb;
		internal ArrangeOverrideCallback arrange_cb;

		Dictionary<DependencyProperty, Expression> expressions = new Dictionary<DependencyProperty, Expression> ();

		private static bool UseNativeLayoutMethod (Type type)
		{
			return type == typeof (FrameworkElement)
				|| type == typeof (Canvas)
				|| type == typeof (Grid);
		}

		private bool OverridesLayoutMethod (string name)
		{
			var method = GetType ().GetMethod (name, BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.FlattenHierarchy);
			if (method == null)
				return false;

			if (!method.IsVirtual || !method.IsFamily)
				return false;

			if (method.ReturnType != typeof (Size))
				return false;

			if (UseNativeLayoutMethod (method.DeclaringType))
				return false;

			var parameters = method.GetParameters ();
			if (parameters.Length != 1 || parameters [0].ParameterType != typeof (Size))
				return false;

			return true;
		}

		private void Initialize ()
		{
			if (OverridesLayoutMethod ("MeasureOverride"))
				measure_cb = new MeasureOverrideCallback (InvokeMeasureOverride);
			if (OverridesLayoutMethod ("ArrangeOverride"))
				arrange_cb = new ArrangeOverrideCallback (InvokeArrangeOverride);
			NativeMethods.framework_element_register_managed_overrides (native, measure_cb, arrange_cb);


			Events.AddOnEventHandler (this, EventIds.UIElement_LoadedEvent, on_loaded);
		}

		public object FindName (string name)
		{
			if (name == null)
				throw new ArgumentNullException ("name");
			return DepObjectFindName (name);
		}

		internal void SetTemplateBinding (DependencyProperty dp, TemplateBindingExpression tb)
		{
			tb.AttachChangeHandler();
			try {
				SetValue (dp, tb);
			} catch {
				// Do nothing here - The DP should still have its default value
			}
		}

		public BindingExpressionBase SetBinding (DependencyProperty dp, Binding binding)
		{
			if (dp == null)
				throw new ArgumentNullException ("dp");
			if (binding == null)
				throw new ArgumentNullException ("binding");

			BindingExpression e = new BindingExpression (binding, this, dp);
			binding.Seal ();
			SetValue (dp, e);
			return e;
		}

		protected virtual Size MeasureOverride (Size availableSize)
		{
			return NativeMethods.framework_element_measure_override (native, availableSize);
		}

		protected virtual Size ArrangeOverride (Size finalSize)
		{
			return NativeMethods.framework_element_arrange_override (native, finalSize);
		}

		public DependencyObject Parent {
			get {
				return NativeDependencyObjectHelper.FromIntPtr (NativeMethods.framework_element_get_logical_parent (native)) as DependencyObject;
			}
		}

		internal DependencyObject SubtreeObject {
			get {
				return NativeDependencyObjectHelper.FromIntPtr (NativeMethods.uielement_get_subtree_object (native)) as DependencyObject;
			}
		}		

		[MonoTODO ("figure out how to construct routed events")]
		public static readonly RoutedEvent LoadedEvent = new RoutedEvent();

		public event EventHandler<ValidationErrorEventArgs> BindingValidationError;

		internal virtual void InvokeOnApplyTemplate ()
		{
			OnApplyTemplate ();
		}

		internal virtual void InvokeLoaded (RoutedEventArgs e)
		{
			InvalidateLocalBindings ();
			InvalidateSubtreeBindings ();

			NativeMethods.event_object_do_emit_current_context (native, EventIds.UIElement_LoadedEvent, e.NativeHandle, false, -1);
		}

		private Size InvokeMeasureOverride (Size availableSize)
		{
			try {
				return MeasureOverride (availableSize);
			} catch (Exception ex) {
				try {
					Console.WriteLine ("Moonlight: Unhandled exception in FrameworkElement.InvokeMeasureOverride: {0}", ex);
				} catch {
					// Ignore
				}
			}
			return new Size (); 
		}

		private Size InvokeArrangeOverride (Size finalSize)
		{
			try {
				return ArrangeOverride (finalSize);
			} catch (Exception ex) {
				try {
					Console.WriteLine ("Moonlight: Unhandled exception in FrameworkElement.InvokeArrangeOverride: {0}", ex);
				} catch {
					// Ignore
				}
			}
			return new Size ();
		}
		
		public virtual void OnApplyTemplate ()
		{
			// according to doc this is not fully implemented since SL templates applies
			// to Control/ContentPresenter and is defined here for WPF compatibility
		}

		internal override void ClearValueImpl (DependencyProperty dp)
		{
			RemoveExpression (dp);
			base.ClearValueImpl (dp);
		}
		
		internal void RaiseBindingValidationError (ValidationErrorEventArgs e)
		{
			EventHandler <ValidationErrorEventArgs> h = BindingValidationError;
			if (h != null)
				h (this, e);
		}

		void RemoveExpression (DependencyProperty dp)
		{
			Expression e;
			if (expressions.TryGetValue (dp, out e)) {
				expressions.Remove (dp);
				e.Dispose ();
			}
		}
		
		internal override void SetValueImpl (DependencyProperty dp, object value)
		{
			bool addingExpression = false;
			Expression existing;
			Expression expression = value as Expression;
			BindingExpressionBase bindingExpression = expression as BindingExpressionBase;
			
			if (bindingExpression != null) {
				if (string.IsNullOrEmpty (bindingExpression.Binding.Path.Path) &&
				    bindingExpression.Binding.Mode == BindingMode.TwoWay)
					throw new ArgumentException ("TwoWay bindings require a non-empty Path");
			}

			expressions.TryGetValue (dp, out existing);
			
			if (expression != null) {
				if (existing != null)
					RemoveExpression (dp);
				expressions.Add (dp, expression);

				addingExpression = true;
				value = expression.GetValue (dp);
			} else if (existing != null) {
				if (existing is BindingExpressionBase) {
					BindingExpressionBase beb = (BindingExpressionBase)existing;

					if (beb.Binding.Mode == BindingMode.TwoWay)
						beb.SetValue (value);
					else if (!(beb.UpdatingSource && beb.Binding.Mode == BindingMode.OneWay)) {
						RemoveExpression (dp);
					}
				}
				else if (existing is TemplateBindingExpression) {
					TemplateBindingExpression tb = (TemplateBindingExpression)existing;

					if (!tb.UpdatingTarget)
						RemoveExpression (dp);
				}
				else {
					RemoveExpression (dp);
				}
			}
			
			try {
				base.SetValueImpl (dp, value);
			} catch {

				if (!addingExpression)
					throw;
				else
					base.SetValueImpl (dp, dp.DefaultValue);
			}
		}

		internal override object ReadLocalValueImpl (DependencyProperty dp)
		{
			Expression expression;
			if (expressions.TryGetValue (dp, out expression))
				return expression;
			return base.ReadLocalValueImpl (dp);
		}
	}
}
