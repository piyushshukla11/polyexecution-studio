package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;

@JsonIgnoreProperties(ignoreUnknown = true)
public class RoomLeaveMessage extends ClientMessage {
    private String roomId;

    public RoomLeaveMessage() {
        setType("room.leave");
    }

    public RoomLeaveMessage(String roomId) {
        setType("room.leave");
        this.roomId = roomId;
    }

    public String getRoomId() {
        return roomId;
    }

    public void setRoomId(String roomId) {
        this.roomId = roomId;
    }
}
