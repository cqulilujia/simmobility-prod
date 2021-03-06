package sim_mob.conf;

import java.awt.Color;
import java.awt.Stroke;
import java.util.Hashtable;

/**
 * Simple interface to our CSS properties like color and line width. 
 * This allows us to switch out CSS parsers without affecting the majority of our code.
 * 
 * \author Seth N. Hetu
 */
public class CSS_Interface {
	//Store properties individually.
	/*package-protected*/ Hashtable<String, Color> backgroundColors;
	/*package-protected*/ Hashtable<String, Color> lineColors; 
	/*package-protected*/ Hashtable<String, Stroke> lineStrokes;
	
	public CSS_Interface() {
		backgroundColors = new Hashtable<String, Color>();
		lineColors = new Hashtable<String, Color>();
		lineStrokes = new Hashtable<String, Stroke>();
	}
	
	//Retrieve a property
	public Color getBackground(String className) {
		return backgroundColors.get(className);
	}
	public Color getLineColor(String className) {
		return lineColors.get(className);
	}
	public Stroke getLineStroke(String className) {
		return lineStrokes.get(className);
	}

}
