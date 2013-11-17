package com.poinsart.votar;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.TimeUnit;

import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.content.Context;
import android.util.Log;
import fi.iki.elonen.NanoHTTPD;

public class VotarWebServer extends NanoHTTPD {
	private MainAct mainact;
	
	public static final int SOCKET_READ_TIMEOUT = 65000;

	public VotarWebServer(int port, MainAct mainact) {
		super(port);
		this.mainact=mainact;
		String ip=getWifiIp();
		if (ip==null)
			Log.i("Votar WebServer", "Starting Votar WebServer but no wifi network detected");
		else
			Log.i("Votar WebServer", "Starting Votar WebServer on http://"+ip+":51285");
	}
	
    private String getWifiIp() {
    	int ip;
    	
    	WifiManager wm=(WifiManager) mainact.getSystemService(Context.WIFI_SERVICE);
    	WifiInfo wi=wm.getConnectionInfo();
    	if (wi==null || wi.getNetworkId()==-1)
    		return null;
    	ip=wi.getIpAddress();
    	return "" + (ip & 0xFF) + "." + ((ip >> 8) & 0xFF) + "." + ((ip >> 16) & 0xFF) + "." + ((ip >> 24 ) & 0xFF);
    }
	
    private Response createResponse(Response.Status status, String mimeType, String message) {
        Response res = new Response(status, mimeType, message);
        res.addHeader("Access-Control-Allow-Origin", "*");
        if (status!=Response.Status.OK)
        	Log.w("Votar WebServer", message);
        return res;
    }
    
    private Response createResponse(Response.Status status, String mimeType, InputStream message) {
        Response res = new Response(status, mimeType, message);
        res.addHeader("Access-Control-Allow-Origin", "*");
        return res;
    }
	
	public Response serve(IHTTPSession session) {
		String uri=session.getUri();
	
		
		Log.i("Votar WebServer", "Request: "+uri);
		
		if (uri.equals("/photo")) {
			if (mainact.photoLock==null) {
				return createResponse(Response.Status.NOT_FOUND, NanoHTTPD.MIME_PLAINTEXT, "Error 404 (NOT FOUND). The file is not ready because no photo has been used yet.");
			}
			try {
				if (!mainact.photoLock.await(60, TimeUnit.SECONDS)) {
					return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). The process was locked for too long and can't deliver the file.");
				}
			} catch (InterruptedException e) {
				return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). The process was interrupted before the server could deliver the file.");
			}
			if (mainact.lastPhotoFilePath==null) {
				return createResponse(Response.Status.NOT_FOUND, NanoHTTPD.MIME_PLAINTEXT, "Error 404 (NOT FOUND). The file is not ready because no photo has been used yet.");
			}
			FileInputStream fis;
			try {
				fis = new FileInputStream(new File(mainact.lastPhotoFilePath));
			} catch (FileNotFoundException e) {
				return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). The server failed while attempting to read the file to deliver.");
			}
			return createResponse(Response.Status.OK, "image/jpeg", fis);
		}
		if (uri.equals("/points")) {
			if (mainact.photoLock==null) {
				return createResponse(Response.Status.NOT_FOUND, NanoHTTPD.MIME_PLAINTEXT, "Error 404 (NOT FOUND). The file is not ready because no photo has been used yet.");
			}
			try {
				if (!mainact.pointsLock.await(60, TimeUnit.SECONDS)) {
					return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). The process was locked for too long and can't deliver the file.");
				}
			} catch (InterruptedException e) {
				return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). The process was interrupted before the server could deliver the file.");
			}
			if (mainact.lastPointsJsonString==null) {
				return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). There was an error in the application and the points data failed to be generated.");
			}
			return createResponse(Response.Status.OK, NanoHTTPD.MIME_PLAINTEXT, mainact.lastPointsJsonString);
		}
		if (uri.equals("/datatimestamp")) {
			return createResponse(Response.Status.OK, NanoHTTPD.MIME_PLAINTEXT, ""+mainact.datatimestamp);
		}
		if (uri.equals("/") || uri.equals("/footer_deco.png") || uri.equals("/votar_logo.png")) {
			String mime, filename;
			if (uri.equals("/")) {
				mime=NanoHTTPD.MIME_HTML;
				filename="index.html";
			} else {
				mime="image/png";
				filename=uri.substring(1);
			}
			InputStream is;
			try {
				is = mainact.assetMgr.open(filename);
			} catch (IOException e) {
				return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). The server failed while attempting to read the static file to deliver.");
			}
			return createResponse(Response.Status.OK, mime, is);
			
		}
		return createResponse(Response.Status.NOT_FOUND, NanoHTTPD.MIME_PLAINTEXT, "Error 404 (NOT FOUND). I'm sorry. My responses are limited. You must ask the right questions.");
	}
}
