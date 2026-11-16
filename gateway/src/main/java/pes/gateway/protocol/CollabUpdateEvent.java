package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class CollabUpdateEvent extends ServerEvent {
    private String roomId;
    private String update;

    public CollabUpdateEvent() {
        super("collab.update");
    }

    public CollabUpdateEvent(String roomId, String update) {
        super("collab.update");
        this.roomId = roomId;
        this.update = update;
    }

    public String getRoomId() {
        return roomId;
    }

    public void setRoomId(String roomId) {
        this.roomId = roomId;
    }

    public String getUpdate() {
        return update;
    }

    public void setUpdate(String update) {
        this.update = update;
    }
}
