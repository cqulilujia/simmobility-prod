package sim_mob.vis.simultion;

import java.awt.geom.Point2D;

public class DriverTick extends AgentTick{

	
	private int ID;
	private boolean fake;
	private int length;
	private int width;
	private int firstFrameID;
	
	public int getID(){return ID;}
	public int getCarLength(){return length;}
	public int getCarWidth() {return width;}
	public boolean getFake() { return fake; }
	public int getFirstFrameID() { return firstFrameID; }

	public void setType(int type){
		this.type = type;
	}
	//TODO : this is test, to be changed
	//private static final int NODE_SIZE = 4;
	//private Path2D.Double poly;

	public DriverTick(double posX, double posY, double angle){
		this.pos = new Point2D.Double(posX,posY);
		this.angle = angle;

	}
	
	public void setItFake(){
		fake = true;
	}
	public void setLenth(int length){
		this.length = length;
	}
	public void setWidth(int width){
		this.width = width;
	}
	public void setID(int id){
		this.ID = id;
	}
	public void setFirstFrameID(int frameID){
		this.firstFrameID = frameID;
	}

}
